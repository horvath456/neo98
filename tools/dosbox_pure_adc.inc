// SPDX-License-Identifier: GPL-2.0-or-later
// ADC transfers are PIC events at the DSP rate. DMA masking and D0/D4 pause
// the transfer; each block raises IRQ8, and 2Ch reloads the 48h block count.
// Included in the private sblaster.cpp clone, never the imported source.
#include "neo98_dbp.h"
static void DSP_ADC_Event(Bitu);
static void DSP_ADC_CallBack(DmaChannel *, DMAEvent);
static void DSP_ADC_Stop() {
    PIC_RemoveEvents(DSP_ADC_Event);
    if (sb.adc.active && sb.dma.chan) sb.dma.chan->Register_Callback(0);
    sb.adc.active = sb.adc.paused = false;
    sb.adc.batch = 0;
}
static void DSP_ADC_Schedule() {
    PIC_RemoveEvents(DSP_ADC_Event);
    if (!sb.adc.active || sb.adc.paused || !sb.dma.chan || sb.dma.chan->masked) return;
    // At most one millisecond per event. The last fraction of a block has
    // its own deadline, so the IRQ is never advanced to the preceding tick.
    Bitu batch = sb.freq / 1000;
    if (!batch) batch = 1;
    if (batch > sb.adc.left) batch = sb.adc.left;
    sb.adc.batch = batch;
    PIC_AddEvent(DSP_ADC_Event, 1000.0 * batch / (sb.freq ? sb.freq : 22050));
}
static void DSP_ADC_CallBack(DmaChannel *, DMAEvent event) {
    if (event == DMA_MASKED) PIC_RemoveEvents(DSP_ADC_Event);
    if (event == DMA_UNMASKED) DSP_ADC_Schedule();
}
static void DSP_ADC_Event(Bitu) {
    if (!sb.adc.active || sb.adc.paused || !sb.dma.chan || sb.dma.chan->masked) return;
    Bit8u bytes[256];
    const Bitu count = sb.adc.batch > sizeof(bytes) ? sizeof(bytes) : sb.adc.batch;
    Neo98DbpState *state = DBP_Neo98State();
    memset(bytes, 128, count);
    if (state->microphone) state->microphone(state->microphone_owner, bytes, count, sb.freq);
    const Bitu done = sb.dma.chan->Write(count, bytes);
    sb.adc.left -= done;
    if (!sb.adc.left) {
        SB_RaiseIRQ(SB_IRQ_8);
        if (sb.adc.autoinit) sb.adc.left = sb.adc.block;
        else { DSP_ADC_Stop(); return; }
    }
    DSP_ADC_Schedule();
}
static void DSP_ADC_Start(Bitu count, bool autoinit) {
    DSP_ADC_Stop();
    DSP_ChangeMode(MODE_NONE);
    PIC_RemoveEvents(END_DMA_Event);
    PIC_RemoveEvents(DMA_Silent_Event);
    sb.dma.mode = DSP_DMA_NONE;
    sb.dma.chan = GetDMAChannel(sb.hw.dma8);
    sb.adc.left = sb.adc.block = count;
    sb.adc.active = true;
    sb.adc.paused = false;
    sb.adc.autoinit = autoinit;
    // Ask the main thread to open the microphone before the first deadline.
    Neo98DbpState *state = DBP_Neo98State();
    if (state->microphone) state->microphone(state->microphone_owner, 0, 0, sb.freq);
    sb.dma.chan->Register_Callback(DSP_ADC_CallBack);
}
static Bit8u DSP_ADC_Direct() {
    Bit8u byte = 128;
    Neo98DbpState *state = DBP_Neo98State();
    if (state->microphone) state->microphone(state->microphone_owner, &byte, 1, 0);
    return byte;
}
