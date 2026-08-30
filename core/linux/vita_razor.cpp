#include "oslib/razor_capture.h"

#ifdef __vita__

#include <psp2/sysmodule.h>

#include "types.h"
#include "cfg/cfg.h"
#include "emulator.h"
#include "oslib/oslib.h"
#if FEAT_SHREC != DYNAREC_NONE
#include "hw/sh4/dyna/blockmanager.h"
#endif
extern "C" {
	SceUInt32 sceRazorCpuIsCapturing(void);
	int sceRazorCpuStartCapture(void);
	int sceRazorCpuStopCapture(void);
	int sceRazorCpuSync(void);
}

namespace {

enum class State
{
	Off,
	Idle,
	Warmup,
	Arming,
	Capturing,
	Done,
};

bool frameSync;
bool autoCapture;
double captureDelay;
double captureLength;
double armTimeout;
State state = State::Off;
double deadline;
double armGiveUp;
double nextArmAttempt;
double captureStarted;
bool armWarned;
bool wasCapturing;

void onGameStart(Event, void *)
{
	if (!autoCapture || state == State::Off || state == State::Done)
		return;
	state = State::Warmup;
	deadline = os_GetSeconds() + captureDelay;
	NOTICE_LOG(COMMON, "RAZOR: armed, capture starts in %.0f s", captureDelay);
}

void stopCapture(const char *why)
{
	if (state == State::Capturing)
	{
		const int rc = sceRazorCpuStopCapture();
		if (rc < 0)
			WARN_LOG(COMMON, "RAZOR: stop capture failed (%s): %#x", why, rc);
		else
			NOTICE_LOG(COMMON, "RAZOR: capture stopped after %.1f s (%s)",
					os_GetSeconds() - captureStarted, why);
	}
	state = State::Done;
}

void onGameStop(Event, void *)
{
	if (state == State::Capturing)
		stopCapture("game stopped");
	else if (state == State::Warmup || state == State::Arming)
		state = State::Idle;
}

void tryArm(double now)
{
	if (now < nextArmAttempt)
		return;
	nextArmAttempt = now + 1.0;
	const int rc = sceRazorCpuStartCapture();
	if (rc >= 0)
	{
		state = State::Capturing;
		captureStarted = now;
		deadline = now + captureLength;
		NOTICE_LOG(COMMON, "RAZOR: capture started");
		return;
	}
	if (!armWarned)
	{
		armWarned = true;
		WARN_LOG(COMMON, "RAZOR: host not listening (%#x), retrying for %.0f s", rc, armTimeout);
	}
	if (now >= armGiveUp)
	{
		ERROR_LOG(COMMON, "RAZOR: gave up waiting for host");
		state = State::Done;
	}
}

}

void razorCaptureInit()
{
	frameSync = cfgLoadBool("perf", "RazorFrameSync", false);
	autoCapture = cfgLoadBool("perf", "RazorAutoCapture", false);
	if (!frameSync && !autoCapture)
		return;
	captureDelay = cfgLoadInt("perf", "RazorCaptureDelay", 45);
	captureLength = cfgLoadInt("perf", "RazorCaptureLength", 0);
	armTimeout = cfgLoadInt("perf", "RazorCaptureArmTimeout", 60);
	const int rc = sceSysmoduleLoadModule(SCE_SYSMODULE_PERF);
	if (rc < 0)
	{
		WARN_LOG(COMMON, "RAZOR: SCE_SYSMODULE_PERF load failed: %#x", rc);
		return;
	}
	state = State::Idle;
	wasCapturing = sceRazorCpuIsCapturing() != 0;
	EventManager::listen(Event::Start, onGameStart);
	EventManager::listen(Event::Terminate, onGameStop);
	EventManager::listen(Event::Pause, onGameStop);
	NOTICE_LOG(COMMON, "RAZOR: hooks active");
}

void razorCaptureTick(bool frameRendered)
{
	if (state == State::Off)
		return;
	if (frameSync && frameRendered)
		sceRazorCpuSync();
	const double now = os_GetSeconds();
	const bool capturing = sceRazorCpuIsCapturing() != 0;
	switch (state)
	{
	case State::Warmup:
		if (now >= deadline)
		{
			state = State::Arming;
			armGiveUp = now + armTimeout;
			nextArmAttempt = 0;
			armWarned = false;
			tryArm(now);
		}
		break;
	case State::Arming:
		tryArm(now);
		break;
	case State::Capturing:
		if (!capturing && wasCapturing)
		{
			NOTICE_LOG(COMMON, "RAZOR: capture stopped by host after %.1f s", now - captureStarted);
			state = State::Done;
		}
		else if (captureLength > 0 && now >= deadline)
			stopCapture("length reached");
		break;
	default:
		break;
	}
#if FEAT_SHREC != DYNAREC_NONE
	if (wasCapturing && !capturing)
		bm_RequestJitBlockDump();
#endif
	wasCapturing = capturing;
}

void razorCaptureTerm()
{
	if (state == State::Capturing)
		stopCapture("shutdown");
	if (state != State::Off)
	{
		EventManager::unlisten(Event::Start, onGameStart);
		EventManager::unlisten(Event::Terminate, onGameStop);
		EventManager::unlisten(Event::Pause, onGameStop);
		state = State::Off;
	}
}

#endif
