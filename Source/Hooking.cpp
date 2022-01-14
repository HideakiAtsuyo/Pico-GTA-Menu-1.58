#pragma once
#include "stdafx.h"

HMODULE _hmoduleDLL;
HANDLE MainFiber;
DWORD WakeTime;

std::vector<LPVOID> Hooking::m_Hooks;
eGameState* Hooking::m_GameState;
GetNumberOfEvents* Hooking::m_GetNumberOfEvents;
GetLabelText* Hooking::m_GetLabelText;
ScriptedGameEvent* Hooking::m_ScriptedGameEvent;
uint64_t* Hooking::m_FrameCount;
std::uint64_t** Hooking::m_WorldPointer;
std::uint64_t** Hooking::m_GlobalBase;
PVOID Hooking::m_ModelSpawnBypass;
GetPlayerName Hooking::m_GetPlayerName;
void* Hooking::m_NativeSpoofer;
static Hooking::NativeRegistrationNew**	m_NativeRegistrationTable;
static std::unordered_map<uint64_t, Hooking::NativeHandler>	m_NativeHandlerCache;

/* Start Hooking */
void Hooking::Start(HMODULE hmoduleDLL)
{
	_hmoduleDLL = hmoduleDLL;
	Log::Init(hmoduleDLL);
	FindPatterns();
	while (*g_Hooking.m_GameState != 0) { Sleep(200); }
	CrossMapping::initNativeMap();
	if (!InitializeHooks()) Cleanup();
}

BOOL Hooking::InitializeHooks()
{
	BOOL ReturnValue = TRUE;

	if (!HookNatives()) {
		Log::Error("Hooking failed!");
		ReturnValue = FALSE;
	}

	return ReturnValue;
}

GetNumberOfEvents* m_OriginalGetNumberOfEvents = nullptr;
std::int32_t GetNumberOfEventsHook(std::int32_t unknown)
{
	if (g_Running)
	{
		static uint64_t LastFrame = 0;
		uint64_t CurrentFrame = *Hooking::m_FrameCount;
		if (LastFrame != CurrentFrame)
		{
			LastFrame = CurrentFrame;
			Hooking::onTickInit();
		}
	}
	else if (IsThreadAFiber())
	{
		ConvertFiberToThread();
	}

	return m_OriginalGetNumberOfEvents(unknown);
}

GetLabelText* m_OriginalGetLabelText = nullptr;
const char* GetLabelTextHook(void* unk, const char* label)
{
	if (!strcmp(label, "HUD_JOINING"))
		return "Isn't Pico Base the fucking best?";
	if (!strcmp(label, "HUD_TRANSP"))
		return "Isn't Pico Base the fucking best?";

	return m_OriginalGetLabelText(unk, label);
}

ScriptedGameEvent* m_OriginalScriptedGameEvent = nullptr;
bool ScriptedGameEventHook(__int64 NetEventStruct, __int64 CNetGamePlayer)
{
	auto Arguments = reinterpret_cast<std::int64_t*>(NetEventStruct + 0x70);
	auto Receiver = *reinterpret_cast<std::int8_t*>(CNetGamePlayer + 0x2D);
	auto Sender = *reinterpret_cast<std::int8_t*>(CNetGamePlayer + 0x35);
	const auto EventHash = Arguments[0];

	if (Receiver == PLAYER::PLAYER_ID())
	{
		if (EventHash == -1479371259)
		{
			Log::Msg("Blocked 'Send to Island' Event from User %s", g_Hooking.m_GetPlayerName(Sender));
			return true;
		}
	}

	return m_OriginalScriptedGameEvent(NetEventStruct, CNetGamePlayer);
}

bool Hooking::HookNatives()
{
	MH_STATUS Status = MH_CreateHook(Hooking::m_GetNumberOfEvents, GetNumberOfEventsHook, (void**)&m_OriginalGetNumberOfEvents);
	if ((Status != MH_OK && Status != MH_ERROR_ALREADY_CREATED) || MH_EnableHook(Hooking::m_GetNumberOfEvents) != MH_OK)
		return false;
	Hooking::m_Hooks.push_back(Hooking::m_GetNumberOfEvents);
	Log::Msg("Hooked: GNOE");

	Status = MH_CreateHook(Hooking::m_GetLabelText, GetLabelTextHook, (void**)&m_OriginalGetLabelText);
	if ((Status != MH_OK && Status != MH_ERROR_ALREADY_CREATED) || MH_EnableHook(Hooking::m_GetLabelText) != MH_OK)
		return false;
	Hooking::m_Hooks.push_back(Hooking::m_GetLabelText);
	Log::Msg("Hooked: GLT");

	Status = MH_CreateHook(Hooking::m_ScriptedGameEvent, ScriptedGameEventHook, (void**)&m_OriginalScriptedGameEvent);
	if ((Status != MH_OK && Status != MH_ERROR_ALREADY_CREATED) || MH_EnableHook(Hooking::m_ScriptedGameEvent) != MH_OK)
		return false;
	Hooking::m_Hooks.push_back(Hooking::m_ScriptedGameEvent);
	Log::Msg("Hooked: SGE");

	return true;
}

void __stdcall ScriptFunction(LPVOID lpParameter)
{
	try
	{
		ScriptMain();

	}
	catch (...)
	{
		Log::Fatal("Failed ScriptFiber");
	}
}

void Hooking::onTickInit()
{
	if (MainFiber == nullptr)
		MainFiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
	
	if (timeGetTime() < WakeTime)
		return;

	static HANDLE ScriptFiber;
	if (ScriptFiber)
		SwitchToFiber(ScriptFiber);
	else
		ScriptFiber = CreateFiber(NULL, ScriptFunction, nullptr);
}

void Hooking::FindPatterns()
{
	Hooking::m_GameState = "83 3D ? ? ? ? ? 75 17 8B 43 20 25"_Scan.add(2).rip(4).as<decltype(Hooking::m_GameState)>(); Log::Msg("Found: GS");
	Hooking::m_GetNumberOfEvents = "48 83 EC 28 33 D2 85 C9"_Scan.as<decltype(Hooking::m_GetNumberOfEvents)>(); Log::Msg("Found: GNOE");
	Hooking::m_GetLabelText = "48 89 5C 24 ? 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 75 44 E8"_Scan.as<decltype(Hooking::m_GetLabelText)>(); Log::Msg("Found: GLT");
	Hooking::m_ScriptedGameEvent = "48 89 44 24 ? 0F 95 C3"_Scan.add(-79).as<decltype(Hooking::m_ScriptedGameEvent)>(); Log::Msg("Found: SGE");
	Hooking::m_FrameCount = "F3 0F 10 0D ? ? ? ? 44 89 6B 08"_Scan.add(4).rip(4).add(-8).as<decltype(Hooking::m_FrameCount)>(); Log::Msg("Found: FC");
	Hooking::m_WorldPointer = "48 8B C3 48 83 C4 20 5B C3 0F B7 05 ? ? ? ?"_Scan.add(-11).add(3).as<decltype(Hooking::m_WorldPointer)>(); Log::Msg("Found: WP");
	Hooking::m_GlobalBase = "4C 8D 4D 08 48 8D 15 ? ? ? ? 4C 8B C0"_Scan.add(7).rip(4).as<decltype(Hooking::m_GlobalBase)>(); Log::Msg("Found: GB");
	Hooking::m_ModelSpawnBypass = "48 8B C8 FF 52 30 84 C0 74 05 48"_Scan.add(8).as<decltype(Hooking::m_ModelSpawnBypass)>(); Log::Msg("Found: MSB");
	Hooking::m_GetPlayerName = "40 53 48 83 EC 20 80 3D ? ? ? ? ? 8B D9 74 22"_Scan.as<decltype(Hooking::m_GetPlayerName)>(); Log::Msg("Found: GPN");
	Hooking::m_NativeSpoofer = "FF E3"_Scan.as<decltype(Hooking::m_NativeSpoofer)>(); Log::Msg("Found: NS");
	scrNativeCallContext::SetVectorResults = "83 79 18 00 48 8B D1 74 4A FF 4A 18"_Scan.as<decltype(scrNativeCallContext::SetVectorResults)>(); Log::Msg("Found: SVR");
	m_NativeRegistrationTable = "48 83 EC 20 48 8D 0D ? ? ? ? E8 ? ? ? ? 0F B7 15 ? ? ? ? 33 FF"_Scan.add(7).rip(4).as<decltype(m_NativeRegistrationTable)>(); Log::Msg("Found: NRT");
}

static Hooking::NativeHandler _Handler(uint64_t origHash)
{
	uint64_t NewHash = CrossMapping::MapNative(origHash);
	if (NewHash == 0)
	{
		return nullptr;
	}

	Hooking::NativeRegistrationNew * RegistrationTable = m_NativeRegistrationTable[NewHash & 0xFF];

	for (; RegistrationTable; RegistrationTable = RegistrationTable->getNextRegistration())
	{
		for (uint32_t i = 0; i < RegistrationTable->getNumEntries(); i++)
		{
			if (NewHash == RegistrationTable->getHash(i))
			{
				return RegistrationTable->handlers[i];
			}
		}
	}
	return nullptr;
}

Hooking::NativeHandler Hooking::GetNativeHandler(uint64_t origHash)
{
	auto& NativeHandler = m_NativeHandlerCache[origHash];

	if (NativeHandler == nullptr)
	{
		NativeHandler = _Handler(origHash);
	}

	return NativeHandler;
}

void WAIT(DWORD ms)
{
	WakeTime = timeGetTime() + ms;
	SwitchToFiber(MainFiber);
}

void __declspec(noreturn) Hooking::Cleanup()
{
	Log::Msg("Cleaning up hooks");
	for (auto func : m_Hooks)
	{
		MH_STATUS Status;
		if ((Status = MH_DisableHook(func)) == MH_OK)
		{
			Log::Msg("Successfully disabled hook %p", func);
		}
		else
		{
			Log::Msg("Failed to disable hook %p (%s)", func, MH_StatusToString(Status));
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	for (auto func : m_Hooks)
	{
		MH_STATUS Status;
		if ((Status = MH_RemoveHook(func)) == MH_OK)
		{
			Log::Msg("Successfully removed hook %p", func);
		}
		else
		{
			Log::Msg("Failed to remove hook %p (%s)", func, MH_StatusToString(Status));
		}
	}

	fclose(stdout);
	FreeConsole();
	FreeLibraryAndExitThread(static_cast<HMODULE>(_hmoduleDLL), 0);
}

MinHookKeepalive::MinHookKeepalive()
{
	MH_Initialize();
}

MinHookKeepalive::~MinHookKeepalive()
{
	MH_Uninitialize();
}

MinHookKeepalive g_MinHookKeepalive;
