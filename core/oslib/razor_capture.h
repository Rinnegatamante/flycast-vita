/* Self-triggering Razor CPU capture for PS Vita. */
#pragma once

#ifdef __vita__
void razorCaptureInit();
void razorCaptureTick(bool frameRendered);
void razorCaptureTerm();
#else
inline void razorCaptureInit() {}
inline void razorCaptureTick(bool) {}
inline void razorCaptureTerm() {}
#endif
