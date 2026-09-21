#include "debug.h"
#include "tunables.h"
#include "utils.h"
#include <math.h>
#include <stdarg.h>

/* -------- non-interactive TUI helpers (debug only) -------- */
#define TUI_W       78
#define TUI_INNER   (TUI_W - 2)
#define C_RESET     "\e[0m"
#define C_BOLD      "\e[1m"
#define C_DIM       "\e[2m"
#define C_ITALIC    "\e[3m"
#define C_CYAN      "\e[36m"
#define C_GREEN     "\e[32m"
#define C_YELLOW    "\e[33m"
#define C_RED       "\e[31m"
#define C_MAGENTA   "\e[35m"

/*
################################
######## DEBUG SNAPSHOT ########
################################
*/

struct DebugSnapshot {
    struct Engine eng;
    struct ECUSchedule sched;
    float ltft[256];
    long long captureMs;
    int valid;
};

static struct DebugSnapshot snap = {0};                 // Latest capture (written under lock)

// High-rate age stats (updated in debugCapture)
static unsigned long long ageOverTargetCount = 0;
static long long peakAgeMs = 0;
static int wasAgeOverTarget = 0;
static unsigned long long ageSumMs = 0;
static unsigned long long ageSampleCount = 0;

// Display-rate bookkeeping
static long long lastPrintMs = 0;
static word16 prevTPS = 0;
static word16 prevRPM = 0;
static word16 prevMAP = 0;
static word16 prevFuelLoad = 0;
static float prevSTFT = 0.0f;
static float prevREALAFR = 0.0f;
static int sampleCount = 0;
static long long peakDeltaTPS = 0;
static word16 peakTPS = 0;
static word16 peakRPM = 0;
static word16 peakFuelLoad = 0;

#define WARN_HOLD_MS    1000
#define WARN_SLOTS      8

struct WarnSlot {
    int active;                 // true this frame
    char msg[48];
    long long sinceMs;          // when warning first went active
    long long clearedMs;        // when it cleared (0 if still active)
};

static struct WarnSlot warnSlots[WARN_SLOTS] = {0};

static void warn_update(int id, int isActive, long long now, const char *msg){
    struct WarnSlot *w = &warnSlots[id];

    if (isActive) {
        if (w->sinceMs == 0) {
            w->sinceMs = now;                           // rising edge
        }
        w->clearedMs = 0;
        w->active = 1;
        snprintf(w->msg, sizeof(w->msg), "%s", msg);
    } else {
        w->active = 0;
        if (w->sinceMs != 0 && w->clearedMs == 0) {
            w->clearedMs = now;                         // start 1s hold
        }
        if (w->clearedMs != 0 && (now - w->clearedMs) >= WARN_HOLD_MS) {
            w->sinceMs = 0;                             // hold expired
            w->clearedMs = 0;
            w->msg[0] = '\0';
        }
    }
}

static int tui_vislen(const char *s){
    int n = 0;
    while (*s) {
        if (s[0] == '\033' && s[1] == '[') {
            s += 2;
            while (*s && !((*s >= '@' && *s <= '~'))) s++;
            if (*s) s++;
            continue;
        }
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            n++;
            s++;
        } else if ((c & 0xE0) == 0xC0) {
            n++;
            s += (s[1] ? 2 : 1);
        } else if ((c & 0xF0) == 0xE0) {
            n++;
            s += (s[1] && s[2]) ? 3 : 1;
        } else if ((c & 0xF8) == 0xF0) {
            n++;
            s += (s[1] && s[2] && s[3]) ? 4 : 1;
        } else {
            s++;
        }
    }
    return n;
}

static void tui_hbar(char *out, size_t outSz, float frac, int width){
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int filled = (int)(frac * (float)width + 0.5f);
    if (filled > width) filled = width;

    size_t pos = 0;
    for (int i = 0; i < width && pos + 4 < outSz; i++) {
        const char *ch = (i < filled) ? "█" : "░";
        size_t n = strlen(ch);
        memcpy(out + pos, ch, n);
        pos += n;
    }
    out[pos] = '\0';
}

static void tui_line(const char *fmt, ...){
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int vis = tui_vislen(buf);
    printf("║");
    fputs(buf, stdout);
    for (int i = vis; i < TUI_INNER; i++) {
        putchar(' ');
    }
    printf("║\n");
}

static void tui_section(const char *title){
    // Left-aligned title so every divider lines up straight
    // Form: ╠═ TITLE ════════...════════╣
    int titleLen = (int)strlen(title);
    int used = 1 + 1 + titleLen + 1; // "═" + " " + title + " "
    int right = TUI_INNER - used;
    if (right < 1) right = 1;

    printf("╠");
    printf("═ %s%s%s ", C_CYAN C_BOLD, title, C_RESET);
    for (int i = 0; i < right; i++) printf("═");
    printf("╣\n");
}

static void tui_divider(void){
    // Full-width section border with no title: ╠════════...════════╣
    printf("╠");
    for (int i = 0; i < TUI_INNER; i++) printf("═");
    printf("╣\n");
}

static void tui_top(void){
    printf("╔");
    for (int i = 0; i < TUI_INNER; i++) printf("═");
    printf("╗\n");
}

static void tui_bottom(void){
    printf("╚");
    for (int i = 0; i < TUI_INNER; i++) printf("═");
    printf("╝\n");
}

static void tui_banner(float hz){
    tui_line("  %s%s    ______________  __     _%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s   / ____/ ____/ / / /____(_)___ ___%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s  / __/ / /   / / / / ___/ / __ `__ \\%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s / /___/ /___/ /_/ (__  ) / / / / / /%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s/_____/\\____/\\____/____/_/_/ /_/ /_/%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s────────────────────────────────────────%s", C_DIM, C_RESET);
    tui_line("  %s%sMade with <3 by Logan Ross%s", C_DIM, C_ITALIC, C_RESET);
    tui_divider();
    tui_line("  %sdebug monitor%s                         %sLIVE%s  %s%4.1f Hz%s",
             C_DIM, C_RESET, C_GREEN C_BOLD, C_RESET, C_DIM, hz, C_RESET);
}

/*
#######################################
######## CAPTURE (UNDER LOCK) ########
#######################################
*/

void debugCapture(struct Engine *eng, struct ECUSchedule *sched){
    long long now = get_time_in_ms();

    // High-rate sampler: count age crossing target every capture
    if (sched != NULL) {
        long long ageMs = now - sched->loopIntervalTimeBase;
        // Only count as overdue if more than 1ms past the target tick
        int ageOver = (ageMs > (loopTime + 1));

        if (ageMs > peakAgeMs) {
            peakAgeMs = ageMs;
        }
        ageSumMs += (unsigned long long)(ageMs < 0 ? 0 : ageMs);
        ageSampleCount++;
        if (ageOver && !wasAgeOverTarget) {
            ageOverTargetCount++;  // rising edge: age just crossed overdue threshold
        }
        wasAgeOverTarget = ageOver;
    }

    // * Snapshot engine + schedule + LTFT table
    snap.eng = *eng;
    if (sched != NULL) {
        snap.sched = *sched;
    }
    memcpy(snap.ltft, LTFT, sizeof(snap.ltft));
    snap.captureMs = now;
    snap.valid = 1;
}

/*
##########################################
######## DISPLAY (OUTSIDE LOCK) #########
##########################################
*/

void debugDisplay(void){
    long long now = get_time_in_ms();

    if (!snap.valid) {
        return;
    }

    if (now - lastPrintMs < 143) {  // Refresh terminal at ~7 Hz
        return;
    }

    // * Local copy so render never races a later capture on this thread
    struct DebugSnapshot s = snap;
    struct Engine *eng = &s.eng;
    struct ECUSchedule *sched = &s.sched;

    float dtSec = (lastPrintMs == 0) ? 0.1f : (float)(now - lastPrintMs) / 1000.0f;
    if (dtSec <= 0.0f) {
        dtSec = 0.1f;
    }

    // --- Derived / rate values ---
    int deltaTPS = (int)eng->TPS - (int)eng->lastTPSValue;
    int tpsRate = (int)((int)eng->TPS - (int)prevTPS) / dtSec;
    int rpmRate = (int)((int)eng->RPM - (int)prevRPM) / dtSec;
    int mapRate = (int)((int)eng->MAP - (int)prevMAP) / dtSec;
    int fuelLoadRate = (int)((int)eng->fuelLoad - (int)prevFuelLoad) / dtSec;
    float stftRate = (eng->STFTCorrection - prevSTFT) / dtSec;
    float afrRate = (eng->REALAFR - prevREALAFR) / dtSec;
    float afrError = eng->REALAFR - eng->AFR_TARGET;
    int mapVacuum = (int)eng->AAP - (int)eng->MAP;
    float throttleFrac = eng->TPS > 100 ? 1.0f : (float)eng->TPS / 100.0f;

    // Fuel-path intermediates (mirrors calculateFuelLoad, read-only)
    word32 flowPerMinTh = (word32)eng->RPM * (word32)eng->displacementPerRev;
    word32 density = (eng->IAT > 0) ? ((word32)eng->MAP * 10 * 1000) / (287 * eng->IAT) : 0;
    word32 realAirFlow = (flowPerMinTh * eng->VE) / 100;
    word32 realAirMass = realAirFlow * density;
    word16 rawFuelLoad = (eng->AFR_TARGET > 0.0f)
        ? (word16)(((realAirMass * 10) / eng->AFR_TARGET) / 60)
        : 0;

    float coldMult = (eng->Coldstart && eng->COOLANT <= eng->coldCoolant) ? (float)coldStartEnrichment : 1.0f;
    float crankMult = eng->EngineCranking ? (float)crankingEnrichment : 1.0f;
    float trimMult = (eng->fuelTrim != 1) ? (float)eng->fuelTrim : 1.0f;
    float toeMult = eng->toeEnrichmentMultiplier;
    float stftMult = 1.0f + (eng->STFTCorrection / 100.0f);
    float ltftMult = (!eng->Coldstart && !eng->EngineCranking)
        ? (1.0f + (eng->LTFTCorrection / 100.0f))
        : 1.0f;
    float predictedLoad = (float)rawFuelLoad * coldMult * crankMult * trimMult * toeMult * stftMult * ltftMult;
    if (predictedLoad > (float)maxFuelLoad) {
        predictedLoad = (float)maxFuelLoad;
    }

    // Table bins
    int mapBin = calculateLowerBinIdx(eng->MAP, mapAxis, MAP_BINS);
    int rpmBin = calculateLowerBinIdx(eng->RPM, rpmAxis, RPM_BINS);
    int veRpmBin = rpmBin > RPM_BINS - 2 ? RPM_BINS - 2 : rpmBin;
    int veIndex = (mapBin * RPM_BINS) + veRpmBin;
    int afrIndex = (mapBin * RPM_BINS) + rpmBin;
    int ltftRpmBin = calculateLowerBinIdx(eng->RPM, LTFTRPMAxis, LTFTRPM_BINS);
    int ltftMapBin = calculateLowerBinIdx(eng->MAP, LTFTMAPAxis, LTFTMAP_BINS);
    int ltftCell = (ltftMapBin * LTFTRPM_BINS) + ltftRpmBin;

    // Running peaks / samples
    sampleCount++;
    if ((long long)labs((long)deltaTPS) > peakDeltaTPS) {
        peakDeltaTPS = labs((long)deltaTPS);
    }
    if (eng->TPS > peakTPS) peakTPS = eng->TPS;
    if (eng->RPM > peakRPM) peakRPM = eng->RPM;
    if (eng->fuelLoad > peakFuelLoad) peakFuelLoad = eng->fuelLoad;

    // Anomaly flags (sticky: hold 1s after clear, track active duration)
    {
        char msg[48];

        snprintf(msg, sizeof(msg), "TPS overflow/wrap (%u > 100)", eng->TPS);
        warn_update(0, eng->TPS > 100, now, msg);

        snprintf(msg, sizeof(msg), "RPM implausibly high (%u)", eng->RPM);
        warn_update(1, eng->RPM > 8000, now, msg);

        snprintf(msg, sizeof(msg), "MAP > AAP (+boost/sensor?)");
        warn_update(2, eng->MAP > eng->AAP + 5, now, msg);

        snprintf(msg, sizeof(msg), "fuelLoad at clamp (%u)", eng->fuelLoad);
        warn_update(3, eng->fuelLoad >= maxFuelLoad, now, msg);

        snprintf(msg, sizeof(msg), "toe enrichment spike (%.2f)", eng->toeEnrichmentMultiplier);
        warn_update(4, eng->toeEnrichmentMultiplier > 5.0f, now, msg);

        snprintf(msg, sizeof(msg), "STFT at limit (%.2f)", eng->STFTCorrection);
        warn_update(5, eng->STFTCorrection >= MAXSTFT || eng->STFTCorrection <= MINSTFT, now, msg);

        snprintf(msg, sizeof(msg), "large AFR error (%.2f)", afrError);
        warn_update(6, fabsf(afrError) > 3.0f, now, msg);

        snprintf(msg, sizeof(msg), "toe enrich active (dTPS=%d)", deltaTPS);
        warn_update(7, deltaTPS >= TPSDeadband, now, msg);
    }

    int warnCount = 0;
    for (int i = 0; i < WARN_SLOTS; i++) {
        if (warnSlots[i].sinceMs != 0) warnCount++;
    }

    // Scheduler timing (from snapshot)
    long long stepAgeMs = s.captureMs - sched->loopIntervalTimeBase;
    int crankDue = 0, tpsDue = 0, stftDue = 0;
    int overdue = (stepAgeMs > (loopTime + 1));
    crankDue = (sched->ECUStep % sched->crankCheckInterval == 0) && !sched->CrankCheckLock;
    tpsDue   = (sched->ECUStep % sched->TPSCheckInterval == 0) && !sched->TPSCheckLock;
    stftDue  = (sched->ECUStep % sched->STFTCheckInterval == 0) && !sched->STFTCheckLock;

    char barTPS[48], barRPM[48], barMAP[48], barVE[48], barLoad[48], barAge[48];
    float tpsFrac = (eng->TPS > 100) ? 1.0f : (float)eng->TPS / 100.0f;
    float rpmFrac = (float)eng->RPM / 6000.0f;
    float mapFrac = (eng->AAP > 0) ? (float)eng->MAP / (float)eng->AAP : 0.0f;
    float veFrac = (float)eng->VE / 110.0f;
    float loadFrac = (float)eng->fuelLoad / (float)maxFuelLoad;
    float ageFrac = (float)stepAgeMs / (float)(loopTime * 2);
    tui_hbar(barTPS, sizeof(barTPS), tpsFrac, 14);
    tui_hbar(barRPM, sizeof(barRPM), rpmFrac, 14);
    tui_hbar(barMAP, sizeof(barMAP), mapFrac, 14);
    tui_hbar(barVE, sizeof(barVE), veFrac, 14);
    tui_hbar(barLoad, sizeof(barLoad), loadFrac, 14);
    tui_hbar(barAge, sizeof(barAge), ageFrac, 14);

    const char *tpsColor = (eng->TPS > 100) ? C_RED : C_GREEN;
    const char *afrColor = (fabsf(afrError) > 3.0f) ? C_RED : (fabsf(afrError) > 1.0f) ? C_YELLOW : C_GREEN;
    const char *ageColor = overdue ? C_RED : (stepAgeMs > loopTime / 2) ? C_YELLOW : C_GREEN;
    float hz = 1.0f / dtSec;
    if (hz > 99.0f) hz = 99.0f;

    // --- TUI frame (display only, not interactive) ---
    printf("\e[?25l\e[H\e[J");  // hide cursor, home, clear
    tui_top();
    tui_banner(hz);
    tui_line("  %st=%-12lld ms%s  samples=%-7d  disp/rev=%-3d  coldThr=%-4d",
             C_DIM, s.captureMs, C_RESET, sampleCount, eng->displacementPerRev, eng->coldCoolant);

    tui_section("SENSORS");
    tui_line("  TPS  %s%s%s %s%5u%%%s   RPM  %s%s%s %s%5u%s",
             tpsColor, barTPS, C_RESET, tpsColor, eng->TPS, C_RESET,
             C_CYAN, barRPM, C_RESET, C_BOLD, eng->RPM, C_RESET);
    tui_line("  MAP  %s%s%s %s%5u%s kPa  AAP            %s%5u%s kPa",
             C_CYAN, barMAP, C_RESET, C_BOLD, eng->MAP, C_RESET,
             C_BOLD, eng->AAP, C_RESET);
    tui_line("  IAT  %5u K       COOL %5u F      OX %4u       trim %5u",
             eng->IAT, eng->COOLANT, eng->OXVoltage, eng->fuelTrim);
    tui_line("  vac  %5d kPa     lastTPS %5u      TIFE %5u      thr %5.2f",
             mapVacuum, eng->lastTPSValue, eng->TIFE, throttleFrac);
    tui_line("  flags  %s%s CRANK%s   %s%s COLD%s",
             eng->EngineCranking ? C_GREEN C_BOLD : C_DIM,
             eng->EngineCranking ? "●" : "○",
             C_RESET,
             eng->Coldstart ? C_YELLOW C_BOLD : C_DIM,
             eng->Coldstart ? "●" : "○",
             C_RESET);

    tui_section("RATES");
    tui_line("  dTPS(sched) %+6d   TPS/s %+7d   RPM/s %+7d   MAP/s %+6d",
             deltaTPS, tpsRate, rpmRate, mapRate);
    tui_line("  fuelLoad/s  %+7d   STFT/s %+7.2f   AFR/s %+7.2f",
             fuelLoadRate, stftRate, afrRate);

    tui_section("FUEL");
    tui_line("  LOAD %s%s%s %s%5u%s      VE   %s%s%s %s%3u%%%s",
             C_MAGENTA, barLoad, C_RESET, C_BOLD, eng->fuelLoad, C_RESET,
             C_CYAN, barVE, C_RESET, C_BOLD, eng->VE, C_RESET);
    tui_line("  AFR_T %6.2f   AFR_R %6.2f   err %s%+6.2f%s",
             eng->AFR_TARGET, eng->REALAFR, afrColor, afrError, C_RESET);
    tui_line("  STFT %+7.2f%%  LTFT %+7.2f%%  toe %6.3f  accum %+6d",
             eng->STFTCorrection, eng->LTFTCorrection,
             eng->toeEnrichmentMultiplier, eng->AFRIntigralAccumulator);

    tui_section("PATH");
    tui_line("  flow/min %-10u density %-8u airFlow %-10u airMass %-10u",
             flowPerMinTh, density, realAirFlow, realAirMass);
    tui_line("  raw %5u  ->  predicted %s%8.1f%s  (clamp %4d)",
             rawFuelLoad, C_BOLD, predictedLoad, C_RESET, maxFuelLoad);
    tui_line("  x cold %5.2f  crank %5.2f  trim %5.2f  toe %5.2f  stft %6.3f  ltft %6.3f",
             coldMult, crankMult, trimMult, toeMult, stftMult, ltftMult);

    tui_section("TABLES");
    tui_line("  MAP bin %2d/%-2d (@%4u)     RPM bin %2d/%-2d (@%5u)",
             mapBin, MAP_BINS - 1, eng->MAP, rpmBin, RPM_BINS - 1, eng->RPM);
    tui_line("  VE[%3d]=%3u (rpmClamp %2d)   AFR[%3d]=%5.2f",
             veIndex, VETable[veIndex], veRpmBin, afrIndex, afrTable[afrIndex]);
    tui_line("  LTFT cell %3d (map %2d, rpm %2d) = %+7.2f",
             ltftCell, ltftMapBin, ltftRpmBin, s.ltft[ltftCell]);

    // LTFT neighborhood: 3x3 around current lower bins, [] = active interp quad
    {
        int rpmLo = ltftRpmBin;
        int rpmHi = (ltftRpmBin < LTFTRPM_BINS - 1) ? ltftRpmBin + 1 : ltftRpmBin;
        int mapLo = ltftMapBin;
        int mapHi = (ltftMapBin < LTFTMAP_BINS - 1) ? ltftMapBin + 1 : ltftMapBin;

        int rpmStart = rpmLo - 1;
        int mapStart = mapLo - 1;
        if (rpmStart < 0) rpmStart = 0;
        if (mapStart < 0) mapStart = 0;
        if (rpmStart > LTFTRPM_BINS - 3) rpmStart = LTFTRPM_BINS - 3;
        if (mapStart > LTFTMAP_BINS - 3) mapStart = LTFTMAP_BINS - 3;

        float rpmWeight = 0.0f;
        float mapWeight = 0.0f;
        if (LTFTRPMAxis[0] > 0) {
            rpmWeight = (float)((int)eng->RPM - (int)LTFTRPMAxis[rpmLo]) / (float)LTFTRPMAxis[0];
        }
        if (LTFTMAPAxis[0] > 0) {
            mapWeight = (float)((int)eng->MAP - (int)LTFTMAPAxis[mapLo]) / (float)LTFTMAPAxis[0];
        }
        if (rpmWeight < 0.0f) rpmWeight = 0.0f;
        if (rpmWeight > 1.0f) rpmWeight = 1.0f;
        if (mapWeight < 0.0f) mapWeight = 0.0f;
        if (mapWeight > 1.0f) mapWeight = 1.0f;

        float shareTL = (1.0f - rpmWeight) * (1.0f - mapWeight);
        float shareTR =        rpmWeight  * (1.0f - mapWeight);
        float shareBL = (1.0f - rpmWeight) *        mapWeight;
        float shareBR =        rpmWeight  *        mapWeight;

        tui_section("LTFT");
        tui_line("  MAP\\RPM  %5u   %5u   %5u     ",
                 LTFTRPMAxis[rpmStart],
                 LTFTRPMAxis[rpmStart + 1],
                 LTFTRPMAxis[rpmStart + 2]);

        for (int mr = 0; mr < 3; mr++) {
            int m = mapStart + mr;
            char c0[16], c1[16], c2[16];
            for (int rr = 0; rr < 3; rr++) {
                int r = rpmStart + rr;
                int idx = (m * LTFTRPM_BINS) + r;
                int active = (m == mapLo || m == mapHi) && (r == rpmLo || r == rpmHi);
                char *dst = (rr == 0) ? c0 : (rr == 1) ? c1 : c2;
                if (active) {
                    snprintf(dst, 16, "[%+5.2f]", s.ltft[idx]);
                } else {
                    snprintf(dst, 16, " %+5.2f ", s.ltft[idx]);
                }
            }
            tui_line("  %3ukPa  %s %s %s",
                     LTFTMAPAxis[m], c0, c1, c2);
        }

        tui_line("  shares  TL %5.2f  TR %5.2f  BL %5.2f  BR %5.2f   corr %+6.2f",
                 shareTL, shareTR, shareBL, shareBR, eng->LTFTCorrection);
        tui_line("  active  TL[%2d,%2d] TR[%2d,%2d] BL[%2d,%2d] BR[%2d,%2d]",
                 mapLo, rpmLo, mapLo, rpmHi, mapHi, rpmLo, mapHi, rpmHi);
    }

    tui_section("SCHEDULER");
    tui_line("  step %s%3d%s/%-3d   age %s%s%s %s%4lld%s/%-2d ms   overdue %s%-3s%s",
             C_BOLD, sched->ECUStep, C_RESET, sched->ECULoopSize,
             ageColor, barAge, C_RESET, ageColor, stepAgeMs, C_RESET, loopTime,
             overdue ? C_RED C_BOLD : C_GREEN, overdue ? "YES" : "no", C_RESET);
    tui_line("  intervals  crank=%-3d  TPS=%-3d  STFT=%-3d     timeBase %-14lld",
             sched->crankCheckInterval, sched->TPSCheckInterval,
             sched->STFTCheckInterval, sched->loopIntervalTimeBase);
    tui_line("  locks      crank=%d    TPS=%d    STFT=%d",
             sched->CrankCheckLock, sched->TPSCheckLock, sched->STFTCheckLock);
    tui_line("  due now    crank=%s%d%s    TPS=%s%d%s    STFT=%s%d%s",
             crankDue ? C_YELLOW C_BOLD : C_DIM, crankDue, C_RESET,
             tpsDue ? C_YELLOW C_BOLD : C_DIM, tpsDue, C_RESET,
             stftDue ? C_YELLOW C_BOLD : C_DIM, stftDue, C_RESET);

    tui_section("PEAKS");
    tui_line("  TPS %-5u   RPM %-5u   fuelLoad %-5u   |dTPS| %-6lld",
             peakTPS, peakRPM, peakFuelLoad, peakDeltaTPS);
    {
        double avgAgeMs = (ageSampleCount > 0)
            ? ((double)ageSumMs / (double)ageSampleCount)
            : 0.0;
        tui_line("  peakAge %s%6lld%s ms    avgAge %s%6.2f%s ms    ageOverTarget %s%-10llu%s",
                 C_BOLD, peakAgeMs, C_RESET,
                 C_BOLD, avgAgeMs, C_RESET,
                 C_BOLD, ageOverTargetCount, C_RESET);
    }

    tui_section("WARNINGS");
    if (warnCount == 0) {
        tui_line("  %s* all clear%s", C_GREEN, C_RESET);
    } else {
        for (int i = 0; i < WARN_SLOTS; i++) {
            struct WarnSlot *w = &warnSlots[i];
            if (w->sinceMs == 0) continue;

            long long endMs = w->active ? now : w->clearedMs;
            float activeSec = (float)(endMs - w->sinceMs) / 1000.0f;
            if (activeSec < 0.0f) activeSec = 0.0f;

            if (w->active) {
                tui_line("  %s!%s %-44s %s%5.1fs%s",
                         C_YELLOW C_BOLD, C_RESET, w->msg, C_DIM, activeSec, C_RESET);
            } else {
                tui_line("  %s!%s %-44s %s%5.1fs held%s",
                         C_DIM, C_RESET, w->msg, C_DIM, activeSec, C_RESET);
            }
        }
    }

    tui_bottom();
    printf("%s  Ctrl+C to exit%s\n", C_DIM, C_RESET);
    fflush(stdout);

    prevTPS = eng->TPS;
    prevRPM = eng->RPM;
    prevMAP = eng->MAP;
    prevFuelLoad = eng->fuelLoad;
    prevSTFT = eng->STFTCorrection;
    prevREALAFR = eng->REALAFR;
    lastPrintMs = now;
}
