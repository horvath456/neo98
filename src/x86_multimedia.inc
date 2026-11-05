// SPDX-License-Identifier: GPL-2.0-or-later
// The multimedia state of a process that is not a device of its own. The mmio
// family moved to src/x86_mmio.cpp on 23 September 2026: its files live in
// guest memory the way WINMM keeps them, so nothing of it stays here.
struct Application::MultimediaState {
};
void Application::FreeMultimedia(){
    FreeWaveOut();
    FreeWaveIn();
    FreeDsoundVxd();
    FreeMidiOut();
    FreeMci();
    FreeDMusic16Ports();
    delete multimedia;multimedia=nullptr;
}
