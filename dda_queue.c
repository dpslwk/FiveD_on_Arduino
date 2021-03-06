#include	"dda_queue.h"

#include	<string.h>
#include	<avr/interrupt.h>

#include	"config.h"
#include	"timer.h"
#include	"serial.h"
#include	"sermsg.h"
#include	"temp.h"
#include	"delay.h"

uint8_t	mb_head = 0;
uint8_t	mb_tail = 0;
DDA movebuffer[MOVEBUFFER_SIZE] __attribute__ ((__section__ (".bss")));

uint8_t queue_full() {
	return (((mb_tail - mb_head - 1) & (MOVEBUFFER_SIZE - 1)) == 0)?255:0;
}

uint8_t queue_empty() {
	return ((mb_tail == mb_head) && (movebuffer[mb_tail].live == 0))?255:0;
}

// -------------------------------------------------------
// This is the one function called by the timer interrupt.
// It calls a few other functions, though.
// -------------------------------------------------------
void queue_step() {
	// do our next step
	if (movebuffer[mb_tail].live) {
		if (movebuffer[mb_tail].waitfor_temp) {
			if (temp_achieved()) {
				movebuffer[mb_tail].live = movebuffer[mb_tail].waitfor_temp = 0;
				serial_writestr_P(PSTR("Temp achieved\n"));
			}

			#if STEP_INTERRUPT_INTERRUPTIBLE
				sei();
			#endif
		}
		else {
			// NOTE: dda_step makes this interrupt interruptible after steps have been sent but before new speed is calculated.
			dda_step(&(movebuffer[mb_tail]));
		}
	}

	// fall directly into dda_start instead of waiting for another step
	// the dda dies not directly after its last step, but when the timer fires and there's no steps to do
	if (movebuffer[mb_tail].live == 0)
		next_move();
}

void enqueue(TARGET *t) {
	// don't call this function when the queue is full, but just in case, wait for a move to complete and free up the space for the passed target
	while (queue_full())
		delay(WAITING_DELAY);

	uint8_t h = mb_head + 1;
	h &= (MOVEBUFFER_SIZE - 1);

	if (t != NULL) {
		dda_create(&movebuffer[h], t);
	}
	else {
		// it's a wait for temp
		movebuffer[h].waitfor_temp = 1;
		movebuffer[h].nullmove = 0;
		#if (F_CPU & 0xFF000000) == 0
			// set "step" timeout to 1 second
			movebuffer[h].c = F_CPU << 8;
		#else
			// set "step" timeout to maximum
			movebuffer[h].c = 0xFFFFFF00;
		#endif
	}

	mb_head = h;

	// fire up in case we're not running yet
	if (timerInterruptIsEnabled() == 0)
		next_move();
}

// sometimes called from normal program execution, sometimes from interrupt context
void next_move() {
	if (queue_empty() == 0) {
		// next item
		uint8_t t = mb_tail + 1;
		t &= (MOVEBUFFER_SIZE - 1);
		dda_start(&movebuffer[t]);
		mb_tail = t;
	}
	else
		disableTimerInterrupt();
}

void print_queue() {
	serial_writechar('Q');
	serwrite_uint8(mb_tail);
	serial_writechar('/');
	serwrite_uint8(mb_head);
	if (queue_full())
		serial_writechar('F');
	if (queue_empty())
		serial_writechar('E');
	serial_writechar('\n');
}
