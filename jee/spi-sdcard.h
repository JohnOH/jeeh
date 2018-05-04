// Driver for SD card access, supports both SD (v1) and SDHC (v2).
//
// Some cards may not work, this does not use slow SPI access during init.

#include <string.h>

template< typename SPI >
struct SdCard {
    constexpr static int TIMEOUT = 50000;  // fairly arbitrary limit

    static bool init () {
        // try *without* and then *with* HCS bit 30 set
        // this determines whether it's an SD (v1) or an SDHC (v2) card
        for (sdhc = 0; sdhc < 2; ++sdhc) {
            // reset SPI register to known state
            SPI::disable();
            for (int i = 0; i < 100; ++i)
                SPI::transfer(0xFF);

            bool ok = false;
            for (int i = 0; !ok && i < 100; ++i)
                ok = cmd(0, 0, 0x95) == 0x01;  // wait for IDLE
            if (!ok)
                break;  // no response, assume no card present

            cmd(8, 0x000001AA, 0x87); wait();  // required by SDHC cards

            for (int i = 0; i < 200; ++i) {
                cmd(55, 0); wait();
                if (cmd(41, sdhc << 30) == 0)
                    return true;
            }
        }
        return false;  // no valid card found
    }

    static void read512 (int page, void* buf) {
        int last = cmd(17, sdhc ? page : page << 9);
        for (int i = 0; i < TIMEOUT; ++i) {
            if (last == 0xFE)
                break;
            last = SPI::transfer(0xFF);
        }
        for (int i = 0; i < 512; ++i)
            ((uint8_t*) buf)[i] = SPI::transfer(0xFF);
        send16b(0xFFFF);
        wait();
    }

    static void write512 (int page, uint8_t const* buf) {
        cmd(24, sdhc ? page : page << 9);
        send16b(0xFFFE);
        for (int i = 0; i < 512; ++i)
            SPI::transfer(((uint8_t const*) buf)[i]);
        send16b(0xFFFF);
        wait();
    }

    static void send16b (uint16_t v) {
        SPI::transfer(v >> 8);
        SPI::transfer(v);
    }

    static int cmd (int req, uint32_t arg, uint8_t crc =0) {
        SPI::disable();

        SPI::enable();
        send16b(0xFF40 | req);
        send16b(arg >> 16);
        send16b(arg);
        SPI::transfer(crc);

        for (int i = 0; i < TIMEOUT; ++i) {
            int r = SPI::transfer(0xFF);
            if ((r & 0x80) == 0)
                return r;
        }

        return -1;
    }

    static void wait () {
        for (int i = 0; i < TIMEOUT; ++i)
            if (SPI::transfer(0xFF) == 0xFF)
                break;
        SPI::disable();
    }

    static uint8_t sdhc; // 0 = SD, 1 = SDHC
};

template< typename SPI >
uint8_t SdCard<SPI>::sdhc;

template< typename T, int N =0 >
struct FatFS {
    typedef T store;

    void init () {
        T::read512(0, buf);                       // find boot sector
        base = *(uint32_t*) (buf+0x1C6);          // base for everything

        T::read512(base, buf);                    // location of boot rec
        spc = buf[0x0D];                          // sectors per cluster
        rsec = *(uint16_t*) (buf+0x0E);           // reserved sectors
        uint8_t nfc = buf[0x10];                  // number of FAT copies
        uint16_t spf = *(uint16_t*) (buf+0x16);   // sectors per fat
        rdir = nfc * spf + rsec + base;           // location of root dir
        rmax = buf[0x11] | buf[0x12]<<8;          // max root entries
        data = (rmax >> 4) + rdir;                // start of data area
        uint32_t tsc = buf[0x13] | buf[0x14]<<8;  // total sector count
        if (tsc == 0)
            tsc = *(uint32_t*) (buf+0x20);        // ... or get 32-bit count
        clim = tsc / spc + 1;                     // cluster limit
#if 0
        printf("base %d spc %d rsec %d nfc %d spf %d"
               " rdir %d rmax %d data %d tsc %d clim %d\n",
            base, spc, rsec, nfc, spf, rdir, rmax, data, tsc, clim);
#endif
        for (int i = 0; i < N; ++i)
            T::read512(base + rsec + i, fat + 512*i);
    }

    bool valid (int cn) {
        if (cn < 2 || cn >= clim)
            return false;
        // is the FAT entry in our buffer?
        return (clim < 4096 ? cn/2*3 : cn*2) < (int) sizeof fat;

    }

    // TODO use buf[] to read on-demand if fat sector is not in memory
    int chain (int cn) {
        if (!valid(cn))
            return 0;

        if (clim >= 4096)  // is it FAT16?
            return ((uint16_t*) fat)[cn];

        int o = cn/2*3;  // else 2 entries per 3 bytes
        return cn & 1 ? fat[o+1]>>4 | fat[o+2]<<4
                      : fat[o] | (fat[o+1]&0xF)<<8;
    }

    void dumpHex (int max =512) {
        for (int i = 0; i < max; i += 16) {
            printf("%3d:", i);
            for (int j = 0; j < 16; ++j)
                printf(" %02x", buf[i+j]);
            printf("\n");
        }
    }

    uint32_t base;          // base sector for everything
    uint32_t rdir;          // location of root dir
    uint32_t data;          // start sector of data area
    uint16_t rmax;          // max root entries
    uint16_t rsec;          // reserved sectors
    uint16_t clim;          // cluster limit
    uint8_t spc;            // sectors per cluster

    uint8_t buf [512];      // buffer space for one sector
    uint8_t fat [N*512+1];  // in-mem copy of FAT, else a dummy byte
};

template< typename T, int N >
struct FileMap {
    FileMap (T& f) : fat (f) {
        memset(map, 0, sizeof map);
    }

    int open (char const name [11]) {
        T::store::read512(fat.rdir, fat.buf);
        for (int i = 0; i < 512; i += 32)
            if (memcmp(name, fat.buf + i, 11) == 0) {
                int cluster = *(uint16_t*) (fat.buf + i + 26);
                int length = *(uint32_t*) (fat.buf + i + 28);
                for (int j = 0; j < 11; ++j) {
                    if (j == 8)
                        printf(".");
                    printf("%c", name[j]);
                }
                int n = 0;
                while (2 <= cluster && cluster < fat.clim && n < N) {
                    // printf("%d,", cluster);
                    map[n++] = cluster;
                    cluster = fat.chain(cluster);
                }
                printf(" %d @ %d, %db\n", n, cluster, length);
                return length;
            }
        return -1;
    }

    bool readSect (int num, void* buf) {
        uint16_t grp = num / fat.spc;
        if (grp >= N || map[grp] == 0)
            return false;
        uint16_t off = map[grp] + num % fat.spc;
        printf("readSect(%d) => %d\n", num, off);
        T::store::read512(off, buf);
        return true;
    }

    bool writeSect (int num, uint8_t const* buf) {
        uint16_t grp = num / fat.spc;
        if (grp >= N || map[grp] == 0)
            return false;
        uint16_t off = map[grp] + num % fat.spc;
        printf("writeSect(%d) => %d\n", num, off);
        T::store::write512(off, buf);
        return true;
    }

    uint16_t map [N];
    T& fat;
};
