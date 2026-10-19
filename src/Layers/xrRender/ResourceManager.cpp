
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrRender_console.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#include "ResourceManager.h"
#include "tss.h"
#include "blenders\blender.h"
#include "blenders\blender_recorder.h"
#include "../../xrCore/_thread_types.h"
#include <tbb/task_arena.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

//	Already defined in Texture.cpp
void fix_texture_name(LPSTR fn);

/*
void fix_texture_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if(  _ext					&&
	  (0==stricmp(_ext,".tga")	||
		0==stricmp(_ext,".dds")	||
		0==stricmp(_ext,".bmp")	||
		0==stricmp(_ext,".ogm")	) )
		*_ext = 0;
}
*/
//--------------------------------------------------------------------------------------------------------------
template <class T>
BOOL reclaim(xr_vector<T*>& vec, const T* ptr)
{
	xr_vector<T*>::iterator it = vec.begin();
	xr_vector<T*>::iterator end = vec.end();
	for (; it != end; it++)
		if (*it == ptr)
		{
			vec.erase(it);
			return TRUE;
		}
	return FALSE;
}

//--------------------------------------------------------------------------------------------------------------
IBlender* CResourceManager::_GetBlender(LPCSTR Name)
{
	R_ASSERT(Name && Name[0]);

	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
#ifdef _EDITOR
	if (I==m_blenders.end())	return 0;
#else
	//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
	if (I == m_blenders.end())
	{
		Msg("DX10: Shader '%s' not found in library.", Name);
		return 0;
	}
#endif
	if (I == m_blenders.end())
	{
		Debug.fatal(DEBUG_INFO, "Shader '%s' not found in library.", Name);
		return 0;
	}
#endif
	else return I->second;
}

IBlender* CResourceManager::_FindBlender(LPCSTR Name)
{
	if (!(Name && Name[0])) return 0;

	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
	if (I == m_blenders.end()) return 0;
	else return I->second;
}

void CResourceManager::ED_UpdateBlender(LPCSTR Name, IBlender* data)
{
	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
	if (I != m_blenders.end())
	{
		R_ASSERT(data->getDescription().CLS == I->second->getDescription().CLS);
		xr_delete(I->second);
		I->second = data;
	}
	else
	{
		m_blenders.insert(mk_pair(xr_strdup(Name), data));
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CResourceManager::_ParseList(sh_list& dest, LPCSTR names)
{
	if (0 == names || 0 == names[0])
		names = "$null";

	ZeroMemory(&dest, sizeof(dest));
	char* P = (char*)names;
	svector<char, 128> N;

	while (*P)
	{
		if (*P == ',')
		{
			// flush
			N.push_back(0);
			strlwr(N.begin());

			fix_texture_name(N.begin());
			//. andy			if (strext(N.begin())) *strext(N.begin())=0;
			dest.push_back(N.begin());
			N.clear();
		}
		else
		{
			N.push_back(*P);
		}
		P++;
	}
	if (N.size())
	{
		// flush
		N.push_back(0);
		strlwr(N.begin());

		fix_texture_name(N.begin());
		//. andy		if (strext(N.begin())) *strext(N.begin())=0;
		dest.push_back(N.begin());
	}
}

ShaderElement* CResourceManager::_CreateElement(ShaderElement& S)
{
	if (S.passes.empty()) return 0;

	xrCriticalSectionGuard guard(creationGuard);

	// Search equal in shaders array
	for (u32 it = 0; it < v_elements.size(); it++)
		if (S.equal(*(v_elements[it]))) return v_elements[it];

	// Create _new_ entry
	ShaderElement* N = xr_new<ShaderElement>(S);
	//N->_copy(S);
	N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_elements.push_back(N);
	return N;
}

void CResourceManager::_DeleteElement(const ShaderElement* S)
{
	xrCriticalSectionGuard guard(creationGuard);
	if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	if (reclaim(v_elements, S)) return;
	Msg("! ERROR: Failed to find compiled 'shader-element'");
}

Shader* CResourceManager::_cpp_Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants,
                                      LPCSTR s_matrices)
{
	xrCriticalSectionGuard guard(creationGuard);

	CBlender_Compile C;
	Shader S;

	//.
	// if (strstr(s_shader,"transparent"))	__asm int 3;

	// Access to template
	C.BT = B;
	C.bEditor = FALSE;
	C.bDetail = FALSE;

#if defined(USE_DX11)
	C.HudElement = false;
#endif

#ifdef _EDITOR
	if (!C.BT)			{ ELog.Msg(mtError,"Can't find shader '%s'",s_shader); return 0; }
	C.bEditor			= TRUE;
#endif

	// Parse names
	_ParseList(C.L_textures, s_textures);
	_ParseList(C.L_constants, s_constants);
	_ParseList(C.L_matrices, s_matrices);

#if defined(USE_DX11)
	if (::Render->hud_loading && RImplementation.o.ssfx_core)
	{
		C.HudElement = true;
	}
#endif

	// Compile element	(LOD0 - HQ)
	{
		C.iElement = 0;
		C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
		//.		C.bDetail			= _GetDetailTexture(*C.L_textures[0],C.detail_texture,C.detail_scaler);
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[0] = _CreateElement(E);
	}

	// Compile element	(LOD1)
	{
		C.iElement = 1;
		//.		C.bDetail			= _GetDetailTexture(*C.L_textures[0],C.detail_texture,C.detail_scaler);
		C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[1] = _CreateElement(E);
	}

	// Compile element
	{
		C.iElement = 2;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[2] = _CreateElement(E);
	}

	// Compile element
	{
		C.iElement = 3;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[3] = _CreateElement(E);
	}

	// Compile element
	{
		C.iElement = 4;
		C.bDetail = TRUE; //.$$$ HACK :)
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[4] = _CreateElement(E);
	}

	// Compile element
	{
		C.iElement = 5;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		S.E[5] = _CreateElement(E);
	}

	// Hacky way to remove from the HUD mask transparent stuff. ( Let's try something better later... )
	if (::Render->hud_loading)
	{
		if (strstr(s_shader, "lens"))
			S.E[0]->passes[0]->ps->hud_disabled = TRUE;
	}

	// Search equal in shaders array
	for (u32 it = 0; it < v_shaders.size(); it++)
		if (S.equal(v_shaders[it])) return v_shaders[it];

	// Create _new_ entry
	Shader* ResultShader = _CreateShader(&S);
	return ResultShader;
}

Shader* CResourceManager::_cpp_Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
		IBlender* pBlender = _GetBlender(s_shader ? s_shader : "null");
		if (!pBlender) return NULL;
		return _cpp_Create(pBlender, s_shader, s_textures, s_constants, s_matrices);
#else	//	USE_DX10
		return _cpp_Create(_GetBlender(s_shader ? s_shader : "null"), s_shader, s_textures, s_constants, s_matrices);
#endif	//	USE_DX10
		//#else
	}
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
	}
	//#endif
}

Shader* CResourceManager::Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		return _cpp_Create(B, s_shader, s_textures, s_constants, s_matrices);
		//#else
	}
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
		//#endif
	}
}

Shader* CResourceManager::Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
		if (_lua_HasShader(s_shader))
			return _lua_Create(s_shader, s_textures);
		else
		{
			Shader* pShader = _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
			if (pShader)
				return pShader;
			else
			{
				if (_lua_HasShader("stub_default"))
					return _lua_Create("stub_default", s_textures);
				else
				{
					FATAL("Can't find stub_default.s");
					return 0;
				}
			}
		}
#else	//	USE_DX10
#ifndef _EDITOR
		if (_lua_HasShader(s_shader))
			return _lua_Create(s_shader, s_textures);
		else
#endif
			return _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
#endif	//	USE_DX10
	}
		//#else
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
	}
	//#endif
}

void CResourceManager::Delete(const Shader* S)
{
	if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED))
		return;

	xrCriticalSectionGuard guard(creationGuard);

	if (reclaim(v_shaders, S))
		return;

	Msg("! ERROR: Failed to find complete shader");
}

// Asynchronous texture loads: new textures from _CreateTexture and reloads from
// CTexture::apply_load. Nothing waits on these; callers that need a texture spin
// on its bLoading flag, which Load() only sets once it is running. A queued load
// that hasn't started yet is never waited on, which is what makes a capped arena
// safe here.
namespace texture_loads
{
	// Unbounded, as before: used when r__texture_upload_threads is 0
	static xr_task_group group;
	// Used when r__texture_upload_threads > 0. Workers only (no slot reserved for
	// a master thread), since tasks are enqueued and nobody joins the arena.
	static std::unique_ptr<tbb::task_arena> arena;
	static int arena_threads = 0;
	static xrCriticalSection submit_lock;
	// Queued or running, in the group or the arena
	static std::atomic<int> pending{0};

	// Process-wide accounting, shared with DeferredUpload
	static std::atomic<int> in_flight{0};
	static std::atomic<int> peak_in_flight{0};
	static std::atomic<u32> completed{0};
	// Textures loaded as STAGING and not yet converted by ProcessStaging() or unloaded
	static std::atomic<int> staging_count{0};
	static std::atomic<u64> staging_bytes{0};
}

void texture_staging_added(u32 bytes)
{
	texture_loads::staging_count.fetch_add(1);
	texture_loads::staging_bytes.fetch_add(bytes);
}

void texture_staging_removed(u32 bytes)
{
	texture_loads::staging_count.fetch_sub(1);
	texture_loads::staging_bytes.fetch_sub(bytes);
}

void texture_load_counted(CTexture* texture)
{
	using namespace texture_loads;
	const int now = in_flight.fetch_add(1) + 1;
	int peak = peak_in_flight.load();
	while (now > peak && !peak_in_flight.compare_exchange_weak(peak, now)) {}

	texture->Load();

	const int remaining = in_flight.fetch_sub(1) - 1;
	// Each count goes to exactly one thread, so each progress line is logged once
	const u32 done = completed.fetch_add(1) + 1;
	if (done % 250 == 0 && Core.isDebug())
	{
		CResourceManager::upload_memory_stats mem_now;
		CResourceManager::sample_process_memory(mem_now);
		Msg("* TexLoad progress: done %u, in-flight now %d, peak %d, staging %d textures / %llu MiB, "
		    "system used %llu MiB", done, remaining, peak_in_flight.load(), staging_count.load(),
		    staging_bytes.load() >> 20, mem_now.system_used_mib);
	}
}

void texture_load_queue(CTexture* texture)
{
	using namespace texture_loads;
	xrCriticalSectionGuard guard(submit_lock);

	// r__texture_upload_threads is applied when nothing is queued or running
	// (between level loads), so no task outlives the arena it was queued in
	const int wanted = std::max(ps_r__texture_upload_threads, 0);
	if (wanted != arena_threads && pending.load() == 0)
	{
		arena.reset();
		if (wanted > 0)
			arena = std::make_unique<tbb::task_arena>(wanted, 0);
		arena_threads = wanted;
		if (wanted > 0)
			Msg("* TexLoad: async texture loads capped at %d threads", wanted);
		else
			Msg("* TexLoad: async texture loads unlimited");
	}

	pending.fetch_add(1);
	auto task = [texture]()
	{
		texture_load_counted(texture);
		texture_loads::pending.fetch_sub(1);
	};
	if (arena)
		arena->enqueue(task);
	else
		group.run(task);
}

void CResourceManager::DeferredUpload()
{
	if (!RDEVICE.b_is_Ready) return;

	Msg("CResourceManager::DeferredUpload -> START, size = %d", m_textures.size());

	CTimer timer;
	timer.Start();

	// Snapshot the pointers under creationGuard so this walk doesn't run
	// concurrently with _CreateTexture's insert into the same std::map, then
	// release the lock before running Load() across PPL worker threads (some
	// of which may themselves need creationGuard, e.g. via _CreateTexture).
	xr_vector<CTexture*> textures;
	{
		xrCriticalSectionGuard guard(creationGuard);
		textures.reserve(m_textures.size());
		for (auto& pair : m_textures)
			textures.push_back(pair.second);
	}

	// -dbg only: how many Load() calls overlap, and what the upload costs in memory
	const bool instrument = Core.isDebug();
	// Peak for this upload's summary; the process-wide count and progress lines
	// come from texture_load_counted
	std::atomic<int> in_flight{0};
	std::atomic<int> peak_in_flight{0};
	const u32 total = u32(textures.size());
	// r__texture_upload_threads > 0 caps the upload to that many threads
	const int upload_threads = ps_r__texture_upload_threads;

	upload_memory_stats mem_before, mem_after;
	if (instrument)
	{
		sample_upload_memory(mem_before);
		Msg("* DeferredUpload start: %u textures, system used %llu MiB, working set %llu MiB, upload threads %d%s",
		    total, mem_before.system_used_mib, mem_before.working_set_mib, upload_threads,
		    upload_threads > 0 ? "" : " (unlimited)");
	}

	auto load = [&](CTexture* texture)
	{
		if (instrument)
		{
			const int now = in_flight.fetch_add(1) + 1;
			int peak = peak_in_flight.load();
			while (now > peak && !peak_in_flight.compare_exchange_weak(peak, now)) {}
		}
		texture_load_counted(texture);
		if (instrument)
			in_flight.fetch_sub(1);
	};

	if (upload_threads > 0)
	{
		tbb::task_arena arena(upload_threads);
		arena.execute([&]
		{
			xr_parallel_foreach(textures.begin(), textures.end(), load);
		});
	}
	else
		xr_parallel_foreach(textures.begin(), textures.end(), load);

	const u32 elapsed_ms = timer.GetElapsed_ms();
	Msg("texture loading time: %d", elapsed_ms);

	if (instrument)
	{
		sample_upload_memory(mem_after);
		Msg("* DeferredUpload: %u textures, peak in-flight %d, system used %llu -> %llu MiB, time %u ms, "
		    "working set %llu -> %llu MiB, texture memory %llu -> %llu MiB, upload threads %d%s",
		    total, peak_in_flight.load(), mem_before.system_used_mib, mem_after.system_used_mib, elapsed_ms,
		    mem_before.working_set_mib, mem_after.working_set_mib, mem_before.texture_mib, mem_after.texture_mib,
		    upload_threads, upload_threads > 0 ? "" : " (unlimited)");
	}
}

// System and process memory only: safe to call from the upload workers (takes no engine locks)
void CResourceManager::sample_process_memory(upload_memory_stats& stats)
{
	MEMORYSTATUSEX status = {};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		stats.system_used_mib = (status.ullTotalPhys - status.ullAvailPhys) >> 20;

	PROCESS_MEMORY_COUNTERS counters = {};
	counters.cb = sizeof(counters);
	if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		stats.working_set_mib = counters.WorkingSetSize >> 20;
}

void CResourceManager::sample_upload_memory(upload_memory_stats& stats)
{
	sample_process_memory(stats);

	u32 m_base, c_base, m_lmaps, c_lmaps;
	{
		xrCriticalSectionGuard guard(creationGuard);
		_GetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
	}
	stats.texture_mib = (u64(m_base) + m_lmaps) >> 20;
}

void CResourceManager::DeferredUnload()
{
	if (!RDEVICE.b_is_Ready)
		return;

	xr_vector<CTexture*> textures;
	{
		xrCriticalSectionGuard guard(creationGuard);
		textures.reserve(m_textures.size());
		for (auto& pair : m_textures)
			textures.push_back(pair.second);
	}

	xr_parallel_foreach(textures.begin(), textures.end(), [](CTexture* texture)
	{
		texture->Unload();
	});
}

void CResourceManager::UnloadAllTexturesOnLevelUnload()
{
	if (!RDEVICE.b_is_Ready)
		return;

    xrCriticalSectionGuard guard(creationGuard);

    xr_vector<CTexture*> textures_to_unload;
    textures_to_unload.reserve(m_textures.size());

    for (const auto& pair : m_textures)
    {
        CTexture* texture = pair.second;
        if (!texture)
            continue;

        if (texture->flags.bUser)
            continue;

        // Keep $ textures alive since they are bound to runtime render targets or other important parts
        if (strstr(*texture->cName, "$"))
            continue;

        // Keep UI textures
        LPCSTR name = pair.first;
        if (strncmp(name, "ui\\", 3) == 0 || strncmp(name, "ui/", 3) == 0)
            continue;

        textures_to_unload.push_back(texture);
    }

    for (CTexture* texture : textures_to_unload)
        texture->Unload();
}

#ifdef _EDITOR
void	CResourceManager::ED_UpdateTextures(AStringVec* names)
{
	// 1. Unload
	if (names){
		for (u32 nid=0; nid<names->size(); nid++)
		{
			map_TextureIt I = m_textures.find	((*names)[nid].c_str());
			if (I!=m_textures.end())	I->second->Unload();
		}
	}else{
		for (map_TextureIt t=m_textures.begin(); t!=m_textures.end(); t++)
			t->second->Unload();
	}

	// 2. Load
	// DeferredUpload	();
}
#endif

Shader* CResourceManager::_CreateShader(Shader* InShader)
{
	xrCriticalSectionGuard guard(creationGuard);

	// Search equal in shaders array
	for (Shader* it : v_shaders)
	{
		if (InShader->equal(it))
			return it;
	}

	// Create _new_ entry
	Shader* N = xr_new<Shader>(*InShader);
	//N->_copy(*InShader);
	N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_shaders.push_back(N);

	return N;
}

void CResourceManager::_GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps)
{
	m_base = c_base = m_lmaps = c_lmaps = 0;

	map_Texture::iterator I = m_textures.begin();
	map_Texture::iterator E = m_textures.end();
	for (; I != E; I++)
	{
		u32 m = I->second->flags.MemoryUsage;
		if (strstr(I->first, "lmap"))
		{
			c_lmaps ++;
			m_lmaps += m;
		}
		else
		{
			c_base ++;
			m_base += m;
		}
	}
}

void CResourceManager::_DumpMemoryUsage()
{
	xr_multimap<u32, std::pair<u32, shared_str>> mtex;

	// sort
	{
		map_Texture::iterator I = m_textures.begin();
		map_Texture::iterator E = m_textures.end();
		for (; I != E; I++)
		{
			u32 m = I->second->flags.MemoryUsage;
			shared_str n = I->second->cName;
			mtex.insert(mk_pair(m, mk_pair(I->second->dwReference.load(std::memory_order_relaxed), n)));
		}
	}

	// dump
	{
		xr_multimap<u32, std::pair<u32, shared_str>>::iterator I = mtex.begin();
		xr_multimap<u32, std::pair<u32, shared_str>>::iterator E = mtex.end();
		for (; I != E; I++)
			Msg("* %4.1f : [%4d] %s", float(I->first) / 1024.f, I->second.first, I->second.second.c_str());
	}
}

void CResourceManager::Evict()
{
	//	TODO: DX10: check if we really need this method
#if !defined(USE_DX10) && !defined(USE_DX11)
	CHK_DX(HW.pDevice->EvictManagedResources());
#endif	//	USE_DX10
}

void CResourceManager::EvictStalledTextures() {
#if defined(USE_DX11)
  if (!ps_r__tex_evict_enabled)
    return;
#ifdef DEBUG
  Msg("* [TexEvict] pass called, frame=%u", RDEVICE.dwFrame);
#endif // DEBUG
  if (!RDEVICE.b_is_Ready)
    return;

  u32 cur_frame = RDEVICE.dwFrame;
  u32 max_age   = (u32)ps_r__tex_evict_age_frames;
#ifdef DEBUG
  u32 evicted      = 0;
  u32 evicted_kb   = 0;
  u32 skip_user    = 0;
  u32 skip_name    = 0;
  u32 skip_unloaded = 0;
  u32 skip_refs    = 0;
  u32 skip_recent  = 0;
#endif // DEBUG

  static xr_string s_evict_cursor;
  const u32 batch_size = (u32)ps_r__tex_evict_batch_size;

  creationGuard.Enter();

  map_Texture::iterator I;
  if (s_evict_cursor.empty()) {
    I = m_textures.begin();
  } else {
    I = m_textures.lower_bound(s_evict_cursor.c_str());
    if (I == m_textures.end())
      I = m_textures.begin();
  }

  u32 scanned = 0;
  for (; I != m_textures.end() && scanned < batch_size; ++I, ++scanned) {
    CTexture *tex = I->second;
    if (tex->flags.bUser) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (tex->cName.size() && strstr(tex->cName.c_str(), "$user$")) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (tex->cName.size() && strstr(tex->cName.c_str(), "$null")) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (!tex->flags.bLoaded) {
#ifdef DEBUG
      skip_unloaded++;
#endif // DEBUG
      continue;
    }
    // UI textures — PDA, inventory icons, etc. — must never be evicted
    LPCSTR name = I->first;
    if (strncmp(name, "ui\\", 3) == 0 || strncmp(name, "ui/", 3) == 0) {
#ifdef DEBUG
      skip_name++;
#endif // DEBUG
      continue;
    }
    if (tex->dwReference.load(std::memory_order_relaxed) > 1) {
#ifdef DEBUG
      u32 refs = tex->dwReference.load(std::memory_order_relaxed);
      if (refs > 2)
        Msg("* [TexEvict] stuck: [%4d] %s", refs, tex->cName.c_str());
      skip_refs++;
#endif // DEBUG
      continue;
    }
    if (cur_frame - tex->dwLastUsedFrame < max_age) {
#ifdef DEBUG
      skip_recent++;
#endif // DEBUG
      continue;
    }

#ifdef DEBUG
    evicted_kb += tex->flags.MemoryUsage / 1024;
#endif // DEBUG
    tex->Unload();
#ifdef DEBUG
    evicted++;
#endif // DEBUG
  }

  s_evict_cursor = (I != m_textures.end()) ? xr_string(I->first) : xr_string();

  creationGuard.Leave();

#ifdef DEBUG
  Msg("* [TexEvict] frame=%u total=%u scanned=%u evicted=%u skip_user=%u "
      "skip_name=%u skip_unloaded=%u skip_refs=%u skip_recent=%u freed_kb=%u",
      cur_frame, (u32)m_textures.size(), scanned, evicted, skip_user, skip_name,
      skip_unloaded, skip_refs, skip_recent, evicted_kb);
#endif // DEBUG
#endif
}


/*
BOOL	CResourceManager::_GetDetailTexture(LPCSTR Name,LPCSTR& T, R_constant_setup* &CS)
{
	LPSTR N = LPSTR(Name);
	map_TD::iterator I = m_td.find	(N);
	if (I!=m_td.end())
	{
		T	= I->second.T;
		CS	= I->second.cs;
		return TRUE;
	} else {
		return FALSE;
	}
}*/
