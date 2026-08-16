#pragma once

// Interrupt-driven BOOT button (PIN_WAKE_BUTTON).
//
// A hold of at least FACTORY_RESET_HOLD_MS fires on_long_press; a shorter press
// fires on_short_press. Both callbacks run in a dedicated task context, so they
// may safely call Matter/display APIs. Either callback may be NULL.
//
// The handler is interrupt-driven and blocks while the button is idle rather
// than polling for it.
void button_init(void (*on_long_press)(void), void (*on_short_press)(void));
