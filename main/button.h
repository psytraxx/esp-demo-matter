#pragma once

// Interrupt-driven BOOT button (PIN_WAKE_BUTTON).
//
// A hold of at least FACTORY_RESET_HOLD_MS fires on_long_press; a shorter press
// fires on_short_press. Both callbacks run in a dedicated task context, so they
// may safely call Matter/display APIs. Either callback may be NULL.
//
// The handler blocks (rather than polling) while the button is idle and arms a
// GPIO light-sleep wake source, so it does not prevent tickless light sleep.
void button_init(void (*on_long_press)(void), void (*on_short_press)(void));
