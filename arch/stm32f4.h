struct Periph {
    constexpr static uint32_t gpio = 0x40020000U;
    constexpr static uint32_t rcc  = 0x40023800U;
};

// interrupt vector table in ram

struct VTable {
    typedef void (*Handler)();

    uint32_t* initial_sp_value;
    Handler
        reset, nmi, hard_fault, memory_manage_fault, bus_fault, usage_fault,
        dummy_x001c[4], sv_call, debug_monitor, dummy_x0034, pend_sv, systick;
    Handler
	wwdg, pvd, tamp_stamp, rtc_wkup, flash, rcc, exti0, exti1, exti2,
        exti3, exti4, dma1_stream0, dma1_stream1, dma1_stream2, dma1_stream3,
	dma1_stream4, dma1_stream5, dma1_stream6, adc, can1_tx, can1_rx0,
	can1_rx1, can1_sce, exti9_5, tim1_brk_tim9, tim1_up_tim10,
	tim1_trg_com_tim11, tim1_cc, tim2, tim3, tim4, i2c1_ev, i2c1_er,
	i2c2_ev, i2c2_er, spi1, spi2, usart1, usart2, usart3, exti15_10,
	rtc_alarm, usb_fs_wkup, tim8_brk_tim12, tim8_up_tim13,
	tim8_trg_com_tim14, tim8_cc, dma1_stream7, fsmc, sdio, tim5, spi3,
	uart4, uart5, tim6_dac, tim7, dma2_stream0, dma2_stream1, dma2_stream2,
	dma2_stream3, dma2_stream4, eth, eth_wkup, can2_tx, can2_rx0, can2_rx1,
	can2_sce, otg_fs, dma2_stream5, dma2_stream6, dma2_stream7, usart6,
	i2c3_ev, i2c3_er, otg_hs_ep1_out, otg_hs_ep1_in, otg_hs_wkup, otg_hs,
	dcmi, cryp, hash_rng, fpu, uart7, uart8, spi4, spi5, spi6, sai1,
	lcd_tft, lcd_tft_err, dma2d;
};

// systick and delays

extern void enableSysTick (uint32_t divider =168000000/1000);

// gpio

template<char port>
struct Port {
    constexpr static uint32_t base    = Periph::gpio + 0x400 * (port-'A');
    constexpr static uint32_t moder   = base + 0x00;
    constexpr static uint32_t typer   = base + 0x04;
    constexpr static uint32_t ospeedr = base + 0x08;
    constexpr static uint32_t pupdr   = base + 0x0C;
    constexpr static uint32_t idr     = base + 0x10;
    constexpr static uint32_t odr     = base + 0x14;
    constexpr static uint32_t bsrr    = base + 0x18;
    constexpr static uint32_t afrl    = base + 0x20;
    constexpr static uint32_t afrh    = base + 0x24;
};

enum class Pinmode {
    // mode (2), typer (1), pupdr (2)
    in_analog        = 0b11000,
    in_float         = 0b00000,
    in_pulldown      = 0b00010,
    in_pullup        = 0b00001,

    out              = 0b01000,
    out_od           = 0b01100,
    alt_out          = 0b10000,
    alt_out_od       = 0b10100,
};

template<char port,int pin>
struct Pin {
    typedef Port<port> gpio;
    constexpr static uint16_t mask = 1U << pin;
    constexpr static int id = 16 * (port-'A') + pin;

    static void mode (Pinmode m, int alt =0) {
        // enable GPIOx clock
        MMIO32(Periph::rcc + 0x30) |= 1 << (port-'A');

        auto mval = static_cast<int>(m);
        constexpr int shift = pin & 15;
        MMIO32(gpio::moder) = (MMIO32(gpio::moder) & ~(3 << 2*shift))
                            | ((mval >> 3) << 2*shift);
        MMIO32(gpio::typer) = (MMIO32(gpio::typer) & ~(1 << shift))
                            | (((mval >> 2) & 1) << shift);
        MMIO32(gpio::pupdr) = (MMIO32(gpio::pupdr) & ~(3 << 2*shift))
                            | ((mval & 3) << 2*shift);

        MMIO32(gpio::ospeedr) = (MMIO32(gpio::ospeedr) & ~(3 << 2*shift))
                              | (0b11 << 2*shift);

        constexpr uint32_t afr = pin & 8 ? gpio::afrh : gpio::afrl;
        constexpr int shift4 = 4 * (pin & 7);
        MMIO32(afr) = (MMIO32(afr) & ~(0xF << shift4)) | (alt << shift4);
    }

    static int read () {
        return mask & MMIO32(gpio::idr) ? 1 : 0;
    }

    static void write (int v) {
        MMIO32(gpio::bsrr) = v ? mask : mask << 16;
    }

    // shorthand
    operator int () const { return read(); }
    void operator= (int v) const { write(v); }

    static void toggle () {
        // both versions below are non-atomic, they access and set in two steps
        // this is smaller and faster (1.6 vs 1.2 MHz on F103 @ 72 MHz):
        // MMIO32(gpio::odr) ^= mask;
        // but this code is safer, because it can't interfere with nearby pins:
        MMIO32(gpio::bsrr) = mask & MMIO32(gpio::odr) ? mask << 16 : mask;
    }
};

// u(s)art

template< typename TX, typename RX >
class UartDev {
public:
    // TODO does not recognise alternate TX pins
    constexpr static int uidx = TX::id ==  9 ? 0 :  // PA9, USART1
                                TX::id ==  2 ? 1 :  // PA2, USART2
                                TX::id == 26 ? 2 :  // PB10, USART3
                                TX::id == 42 ? 3 :  // PC10, UART4
                                TX::id == 44 ? 4 :  // PC12, UART5
                                               0;   // else USART1
    constexpr static uint32_t base = uidx == 0 ? 0x40011000 :
                                                 0x40004000 + 0x400 * uidx;
    constexpr static uint32_t sr  = base + 0x00;
    constexpr static uint32_t dr  = base + 0x04;
    constexpr static uint32_t brr = base + 0x08;
    constexpr static uint32_t cr1 = base + 0x0C;

    UartDev () {
        tx.mode(Pinmode::alt_out, 7);
        rx.mode(Pinmode::in_pullup, 7);

        if (uidx == 0)
            MMIO32(Periph::rcc + 0x44) |= 1 << 4; // enable USART1 clock
        else
            MMIO32(Periph::rcc + 0x40) |= 1 << (16+uidx); // U(S)ART 2..5

        MMIO32(brr) = 729;  // 115200 baud @ 84 MHz
        MMIO32(cr1) = (1<<13) | (1<<3) | (1<<2);  // UE, TE, RE
    }

    static bool writable () {
        return (MMIO32(sr) & 0x80) != 0;  // TXE
    }

    static void putc (int c) {
        while (!writable())
            ;
        MMIO32(dr) = (uint8_t) c;
    }

    static bool readable () {
        return (MMIO32(sr) & 0x24) != 0;  // RXNE or ORE
    }

    static int getc () {
        while (!readable())
            ;
        return MMIO32(dr);
    }

    static TX tx;
    static RX rx;
};

template< typename TX, typename RX >
TX UartDev<TX,RX>::tx;

template< typename TX, typename RX >
RX UartDev<TX,RX>::rx;

// interrupt-enabled uart, sits of top of polled uart

template< typename TX, typename RX, int N =50 >
class UartBufDev {
public:
    UartBufDev () {
        auto handler = []() {
            if (uart.readable()) {
                int c = uart.getc();
                if (recv.free())
                    recv.put(c);
                // else discard the input
            }
            if (uart.writable()) {
                if (xmit.avail() > 0)
                    uart.putc(xmit.get());
                else
                    MMIO32(uart.cr1) &= ~(1<<7);  // disable TXEIE
            }
        };

        switch (uart.uidx) {
            case 0: VTableRam().usart1 = handler; break;
            case 1: VTableRam().usart2 = handler; break;
            case 2: VTableRam().usart3 = handler; break;
            case 3: VTableRam().uart4  = handler; break;
            case 4: VTableRam().uart5  = handler; break;
        }

        // nvic interrupt numbers are 37, 38, 39, 52, and 53, respectively
        constexpr uint32_t nvic_en1r = 0xE000E104;
        constexpr int irq = (uart.uidx < 3 ? 37 : 49) + uart.uidx;
        MMIO32(nvic_en1r) |= 1 << (irq-32);  // enable USART interrupt

        MMIO32(uart.cr1) |= (1<<5);  // enable RXNEIE
    }

    static bool writable () {
        return xmit.free();
    }

    static void putc (int c) {
        while (!writable())
            ;
        xmit.put(c);
        MMIO32(uart.cr1) |= (1<<7);  // enable TXEIE
    }

    static bool readable () {
        return recv.avail() > 0;
    }

    static int getc () {
        while (!readable())
            ;
        return recv.get();
    }

    static UartDev<TX,RX> uart;
    static RingBuffer<N> recv;
    static RingBuffer<N> xmit;
};

template< typename TX, typename RX, int N >
UartDev<TX,RX> UartBufDev<TX,RX,N>::uart;

template< typename TX, typename RX, int N >
RingBuffer<N> UartBufDev<TX,RX,N>::recv;

template< typename TX, typename RX, int N >
RingBuffer<N> UartBufDev<TX,RX,N>::xmit;
