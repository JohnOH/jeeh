#include <jee.h>

UartDev< PinA<9>, PinA<10> > uart;

void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    veprintf(uart.putc, fmt, ap);
    va_end(ap);
}

// the above is standard code for a debug console with printf support

int main () {
    printf("%%b <%b> %%3o <%3o> %%d <%d> %%04x <%04x> %%2c <%2c> %%2s <%2s>\n",
            '!', '!', '!', '!', '!', "!");
}

// output:
//  %b <100001> %3o < 41> %d <33> %04x <0021> %2c < !> %2s <! >
