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
#define C_TITLE     "\e[1;3;31m"   // bold italic red — section headers
#define C_FRAME     "\e[2m"        // dim frame ink

// Heavy box-drawing (thicker instrument-panel look)
#define BX_TL   "┏"
#define BX_TR   "┓"
#define BX_BL   "┗"
#define BX_BR   "┛"
#define BX_H    "━"
#define BX_V    "┃"
#define BX_ML   "┣"
#define BX_MR   "┫"

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
    long long stepAgeMs;
    int crankRan;
    int toeRan;
    int stftRan;
    int valid;
};

static struct DebugSnapshot snap = {0};                 // Latest capture (written under lock)

// High-rate age / session stats (updated in debugCapture)
static unsigned long long ageOverTargetCount = 0;
static long long peakAgeMs = 0;
static int wasAgeOverTarget = 0;
static unsigned long long ageSumMs = 0;
static unsigned long long ageSampleCount = 0;
static unsigned long long skipDebt = 0;
static int overdueTicksPeak = 0;

static long long firstCaptureMs = 0;
static long long lastCaptureMs = 0;
static unsigned long long captureCount = 0;
static unsigned long long stftUpdateCount = 0;
static unsigned long long ltftWriteCount = 0;
static long long lastLtftWriteMs = 0;
static long long warmMs = 0;            // time not cold/cranking (LTFT allowed)
static long long afrLeanMs = 0;
static long long afrRichMs = 0;
static long long afrInbandMs = 0;
static float peakAbsSTFT = 0.0f;
static float peakAbsLTFT = 0.0f;
static float peakAfrErr = 0.0f;

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

/* -------- display smoothing (EMA, visual only) -------- */
#define DBG_SMOOTH_A    0.35f
#define DBG_SMOOTH_AR   0.22f

static int   smReady = 0;
static float smTPS, smRPM, smMAP, smAAP, smIAT, smCool, smVac, smThr;
static float smLoad, smVE, smAfrT, smAfrR, smAfrErr, smSTFT, smLTFT, smToe;
static float smRawLoad, smPredLoad;
static float smFlow, smDens, smAirFlow, smAirMass;
static float smTpsRate, smRpmRate, smMapRate, smFuelRate, smStftRate, smAfrRate;
static float smDeltaTPS, smAge, smStftStep, smHead;
static float smShareTL, smShareTR, smShareBL, smShareBR;
static float smLtftCell, smVeLo, smVeHi, smVeRatio;
static float smColdM, smCrankM, smTrimM, smToeM, smStftM, smLtftM;
static float smCCold, smCCrank, smCTrim, smCToe, smCStft, smCLtft;

static float dbg_smooth(float *state, float sample, float alpha){
    if (!smReady) {
        *state = sample;
        return sample;
    }
    *state += alpha * (sample - *state);
    return *state;
}

#define WARN_HOLD_MS    1000
#define WARN_SLOTS      8

struct WarnSlot {
    int active;
    char msg[48];
    long long sinceMs;
    long long clearedMs;
};

static struct WarnSlot warnSlots[WARN_SLOTS] = {0};

static void warn_update(int id, int isActive, long long now, const char *msg){
    struct WarnSlot *w = &warnSlots[id];

    if (isActive) {
        if (w->sinceMs == 0) {
            w->sinceMs = now;
        }
        w->clearedMs = 0;
        w->active = 1;
        snprintf(w->msg, sizeof(w->msg), "%s", msg);
    } else {
        w->active = 0;
        if (w->sinceMs != 0 && w->clearedMs == 0) {
            w->clearedMs = now;
        }
        if (w->clearedMs != 0 && (now - w->clearedMs) >= WARN_HOLD_MS) {
            w->sinceMs = 0;
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
        const char *ch = (i < filled) ? "▰" : "▱";
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

    // Print at most TUI_INNER visible cols so the right border never drifts
    int vis = 0;
    const char *p = buf;
    printf("%s%s%s", C_FRAME, BX_V, C_RESET);
    while (*p && vis < TUI_INNER) {
        if (p[0] == '\033' && p[1] == '[') {
            fputc(*p++, stdout);
            fputc(*p++, stdout);
            while (*p && !((*p >= '@' && *p <= '~'))) {
                fputc(*p++, stdout);
            }
            if (*p) fputc(*p++, stdout);
            continue;
        }
        unsigned char c = (unsigned char)*p;
        if (c < 0x80) {
            fputc(*p++, stdout);
            vis++;
        } else if ((c & 0xE0) == 0xC0) {
            fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            vis++;
        } else if ((c & 0xF0) == 0xE0) {
            fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            vis++;
        } else if ((c & 0xF8) == 0xF0) {
            fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            if (*p) fputc(*p++, stdout);
            vis++;
        } else {
            p++;
        }
    }
    printf("%s", C_RESET);  // never leak color into the border
    for (int i = vis; i < TUI_INNER; i++) {
        putchar(' ');
    }
    printf("%s%s%s\n", C_FRAME, BX_V, C_RESET);
}

static void tui_section(const char *title){
    // Form: ┣━ ▸ TITLE ━━━━━━━━━━━━━━━━━━━┫
    int titleLen = (int)strlen(title);
    int used = 1 + 1 + 1 + 1 + titleLen + 1; // ━ + sp + ▸ + sp + title + sp
    int right = TUI_INNER - used;
    if (right < 1) right = 1;

    printf("%s%s%s", C_FRAME, BX_ML, C_RESET);
    printf("%s%s%s ▸ %s%s%s ", C_FRAME, BX_H, C_RESET, C_TITLE, title, C_RESET);
    printf("%s", C_FRAME);
    for (int i = 0; i < right; i++) printf("%s", BX_H);
    printf("%s%s\n", BX_MR, C_RESET);
}

static void tui_divider(void){
    printf("%s%s", C_FRAME, BX_ML);
    for (int i = 0; i < TUI_INNER; i++) printf("%s", BX_H);
    printf("%s%s\n", BX_MR, C_RESET);
}

static void tui_top(void){
    printf("%s%s▶", C_FRAME, BX_TL);
    for (int i = 0; i < TUI_INNER - 1; i++) printf("%s", BX_H);
    printf("%s%s\n", BX_TR, C_RESET);
}

static void tui_bottom(void){
    printf("%s%s", C_FRAME, BX_BL);
    for (int i = 0; i < TUI_INNER - 1; i++) printf("%s", BX_H);
    printf("◀%s%s\n", BX_BR, C_RESET);
}

static void tui_banner(float hz, float loopHz, float upSec){
    tui_line("  %s%s    ______________  __     _%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s   / ____/ ____/ / / /____(_)___ ___%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s  / __/ / /   / / / / ___/ / __ `__ \\%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s / /___/ /___/ /_/ (__  ) / / / / / /%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s%s/_____/\\____/\\____/____/_/_/ /_/ /_/%s", C_BOLD, C_CYAN, C_RESET);
    tui_line("  %s────────────────────────────────────────%s", C_DIM, C_RESET);
    tui_line("  %s%sMade with <3 by Logan Ross%s", C_DIM, C_ITALIC, C_RESET);
    tui_divider();
    tui_line("  %stelemetry%s   %s● LIVE%s  %s%4.1f Hz%s   loop %s%5.0f%s Hz   up %s%6.1fs%s",
             C_DIM, C_RESET,
             C_GREEN C_BOLD, C_RESET, C_DIM, hz, C_RESET,
             C_BOLD, loopHz, C_RESET,
             C_DIM, upSec, C_RESET);
}

static int next_due_ms(int step, int interval, int locked){
    if (interval <= 0) return 0;
    int mod = step % interval;
    if (mod == 0) {
        return locked ? (interval * loopTime) : 0;
    }
    return (interval - mod) * loopTime;
}

static int slot_ran(int step, int interval, int locked){
    if (interval <= 0) return 0;
    return (step % interval == 0) && locked;
}

/*
#######################################
######## CAPTURE (UNDER LOCK) ########
#######################################
*/

void debugCapture(struct Engine *eng, struct ECUSchedule *sched){
    long long now = get_time_in_ms();

    if (firstCaptureMs == 0) {
        firstCaptureMs = now;
        lastCaptureMs = now;
    }

    long long dtMs = now - lastCaptureMs;
    if (dtMs < 0) dtMs = 0;
    captureCount++;

    if (sched != NULL) {
        long long ageMs = now - sched->loopIntervalTimeBase;
        int ageOver = (ageMs > (loopTime + 1));

        if (ageMs > peakAgeMs) {
            peakAgeMs = ageMs;
        }
        ageSumMs += (unsigned long long)(ageMs < 0 ? 0 : ageMs);
        ageSampleCount++;
        if (ageOver && !wasAgeOverTarget) {
            ageOverTargetCount++;
        }
        wasAgeOverTarget = ageOver;

        if (ageMs > (loopTime + 1)) {
            int ticks = (int)(ageMs / loopTime);
            if (ticks > overdueTicksPeak) overdueTicksPeak = ticks;
        } else if (overdueTicksPeak > 1) {
            skipDebt += (unsigned long long)(overdueTicksPeak - 1);
            overdueTicksPeak = 0;
        } else {
            overdueTicksPeak = 0;
        }

        snap.stepAgeMs = ageMs;
        snap.crankRan = slot_ran(sched->ECUStep, sched->crankCheckInterval, sched->CrankCheckLock);
        snap.toeRan   = slot_ran(sched->ECUStep, sched->TPSCheckInterval,   sched->TPSCheckLock);
        snap.stftRan  = slot_ran(sched->ECUStep, sched->STFTCheckInterval,  sched->STFTCheckLock);
        snap.sched = *sched;
    }

    // STFT is always closed-loop on O2; track warm time (when LTFT may apply)
    if (!eng->Coldstart && !eng->EngineCranking) {
        warmMs += dtMs;
    }

    float afrErr = eng->REALAFR - eng->AFR_TARGET;
    if (fabsf(afrErr) < 0.50f) {
        afrInbandMs += dtMs;
    } else if (afrErr > 0.0f) {
        afrLeanMs += dtMs;
    } else {
        afrRichMs += dtMs;
    }
    if (fabsf(afrErr) > peakAfrErr) peakAfrErr = fabsf(afrErr);

    float absSTFT = fabsf(eng->STFTCorrection);
    float absLTFT = fabsf(eng->LTFTCorrection);
    if (absSTFT > peakAbsSTFT) peakAbsSTFT = absSTFT;
    if (absLTFT > peakAbsLTFT) peakAbsLTFT = absLTFT;

    if (snap.stftRan) {
        stftUpdateCount++;
        if (eng->STFTCorrection > STFTDEADBAND || eng->STFTCorrection < -STFTDEADBAND) {
            ltftWriteCount++;
            lastLtftWriteMs = now;
        }
    }

    snap.eng = *eng;
    memcpy(snap.ltft, LTFT, sizeof(snap.ltft));
    snap.captureMs = now;
    snap.valid = 1;
    lastCaptureMs = now;
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

    struct DebugSnapshot s = snap;
    struct Engine *eng = &s.eng;
    struct ECUSchedule *sched = &s.sched;

    float dtSec = (lastPrintMs == 0) ? 0.1f : (float)(now - lastPrintMs) / 1000.0f;
    if (dtSec <= 0.0f) {
        dtSec = 0.1f;
    }

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
    int ltftApplied = (!eng->Coldstart && !eng->EngineCranking);
    float ltftMult = ltftApplied ? (1.0f + (eng->LTFTCorrection / 100.0f)) : 1.0f;

    float x = (float)rawFuelLoad;
    float cCold = x * (coldMult - 1.0f);   x += cCold;
    float cCrank = x * (crankMult - 1.0f); x += cCrank;
    float cTrim = x * (trimMult - 1.0f);   x += cTrim;
    float cToe = x * (toeMult - 1.0f);     x += cToe;
    float cStft = x * (stftMult - 1.0f);   x += cStft;
    float cLtft = x * (ltftMult - 1.0f);   x += cLtft;
    float unclamped = x;
    float predictedLoad = (unclamped > (float)maxFuelLoad) ? (float)maxFuelLoad : unclamped;

    float stftStep = afrError * (float)STFTCorrectionDamper;
    float stftHead = 0.0f;
    if (eng->STFTCorrection >= 0.0f) {
        stftHead = eng->STFTCorrection - (float)STFTDEADBAND;
    } else {
        stftHead = (-eng->STFTCorrection) - (float)STFTDEADBAND;
    }
    int learning = (eng->STFTCorrection > STFTDEADBAND) || (eng->STFTCorrection < -STFTDEADBAND);
    const char *ltftDir = learning
        ? ((eng->STFTCorrection > 0.0f) ? "+" : "-")
        : "idle";
    float totalTrim = stftMult * ltftMult;

    float toeCand = 1.0f;
    if (deltaTPS >= TPSDeadband) {
        toeCand = (float)deltaTPS * (float)toeEnrichment;
        if (toeCand < 1.0f) toeCand = 1.0f;
    }
    int toeActive = (eng->toeEnrichmentMultiplier > 1.005f) || (deltaTPS >= TPSDeadband);

    int crankMargin = (int)eng->RPM - CRANKING_RPM;
    int coolMargin = (int)eng->COOLANT - (int)eng->coldCoolant;

    int mapBin = calculateLowerBinIdx(eng->MAP, mapAxis, MAP_BINS);
    int rpmBin = calculateLowerBinIdx(eng->RPM, rpmAxis, RPM_BINS);
    int veRpmBin = rpmBin > RPM_BINS - 2 ? RPM_BINS - 2 : rpmBin;
    int veIndex = (mapBin * RPM_BINS) + veRpmBin;
    int afrIndex = (mapBin * RPM_BINS) + rpmBin;
    int ltftRpmBin = calculateLowerBinIdx(eng->RPM, LTFTRPMAxis, LTFTRPM_BINS);
    int ltftMapBin = calculateLowerBinIdx(eng->MAP, LTFTMAPAxis, LTFTMAP_BINS);
    int ltftCell = (ltftMapBin * LTFTRPM_BINS) + ltftRpmBin;

    int16_t rpmDelta = (int16_t)((int)eng->RPM - (int)rpmAxis[veRpmBin]);
    if (rpmDelta < 0) rpmDelta = 0;
    if (rpmDelta > (int16_t)rpmAxis[0]) rpmDelta = (int16_t)rpmAxis[0];
    float veRatio = (rpmAxis[0] > 0) ? ((float)rpmDelta / (float)rpmAxis[0]) : 0.0f;
    float veLo = (float)VETable[veIndex];
    float veHi = (float)VETable[veIndex + 1];

    int afrRpmHi = (rpmBin < RPM_BINS - 1) ? rpmBin + 1 : rpmBin;
    int afrMapHi = (mapBin < MAP_BINS - 1) ? mapBin + 1 : mapBin;
    float afrHere = afrTable[afrIndex];
    float afrRpmN = afrTable[(mapBin * RPM_BINS) + afrRpmHi];
    float afrMapN = afrTable[(afrMapHi * RPM_BINS) + rpmBin];

    // LTFT table stats
    float ltftMin = 0.0f, ltftMax = 0.0f, ltftSum = 0.0f;
    int ltftNz = 0;
    for (int i = 0; i < 256; i++) {
        float v = s.ltft[i];
        if (i == 0) { ltftMin = v; ltftMax = v; }
        if (v < ltftMin) ltftMin = v;
        if (v > ltftMax) ltftMax = v;
        ltftSum += v;
        if (fabsf(v) > 0.001f) ltftNz++;
    }
    float ltftMean = ltftSum / 256.0f;

    sampleCount++;
    if ((long long)labs((long)deltaTPS) > peakDeltaTPS) {
        peakDeltaTPS = labs((long)deltaTPS);
    }
    if (eng->TPS > peakTPS) peakTPS = eng->TPS;
    if (eng->RPM > peakRPM) peakRPM = eng->RPM;
    if (eng->fuelLoad > peakFuelLoad) peakFuelLoad = eng->fuelLoad;

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

    long long stepAgeMs = s.stepAgeMs;
    int overdue = (stepAgeMs > (loopTime + 1));
    int crankDueMs = next_due_ms(sched->ECUStep, sched->crankCheckInterval, sched->CrankCheckLock);
    int tpsDueMs   = next_due_ms(sched->ECUStep, sched->TPSCheckInterval,   sched->TPSCheckLock);
    int stftDueMs  = next_due_ms(sched->ECUStep, sched->STFTCheckInterval,  sched->STFTCheckLock);

    float dDeltaTPS = dbg_smooth(&smDeltaTPS, (float)deltaTPS,     DBG_SMOOTH_AR);
    float dTpsRate  = dbg_smooth(&smTpsRate,  (float)tpsRate,      DBG_SMOOTH_AR);
    float dRpmRate  = dbg_smooth(&smRpmRate,  (float)rpmRate,      DBG_SMOOTH_AR);
    float dMapRate  = dbg_smooth(&smMapRate,  (float)mapRate,      DBG_SMOOTH_AR);
    float dFuelRate = dbg_smooth(&smFuelRate, (float)fuelLoadRate, DBG_SMOOTH_AR);
    float dStftRate = dbg_smooth(&smStftRate, stftRate,            DBG_SMOOTH_AR);
    float dAfrRate  = dbg_smooth(&smAfrRate,  afrRate,             DBG_SMOOTH_AR);

    float dTPS  = dbg_smooth(&smTPS,     (float)eng->TPS,              DBG_SMOOTH_A);
    float dRPM  = dbg_smooth(&smRPM,     (float)eng->RPM,              DBG_SMOOTH_A);
    float dMAP  = dbg_smooth(&smMAP,     (float)eng->MAP,              DBG_SMOOTH_A);
    float dAAP  = dbg_smooth(&smAAP,     (float)eng->AAP,              DBG_SMOOTH_A);
    float dIAT  = dbg_smooth(&smIAT,     (float)eng->IAT,              DBG_SMOOTH_A);
    float dCool = dbg_smooth(&smCool,    (float)eng->COOLANT,          DBG_SMOOTH_A);
    float dVac  = dbg_smooth(&smVac,     (float)mapVacuum,             DBG_SMOOTH_A);
    float dThr  = dbg_smooth(&smThr,     throttleFrac,                 DBG_SMOOTH_A);
    float dLoad = dbg_smooth(&smLoad,    (float)eng->fuelLoad,         DBG_SMOOTH_A);
    float dVE   = dbg_smooth(&smVE,      (float)eng->VE,               DBG_SMOOTH_A);
    float dAfrT = dbg_smooth(&smAfrT,    eng->AFR_TARGET,              DBG_SMOOTH_A);
    float dAfrR = dbg_smooth(&smAfrR,    eng->REALAFR,                 DBG_SMOOTH_A);
    float dAfrE = dbg_smooth(&smAfrErr,  afrError,                     DBG_SMOOTH_A);
    float dSTFT = dbg_smooth(&smSTFT,    eng->STFTCorrection,          DBG_SMOOTH_A);
    float dLTFT = dbg_smooth(&smLTFT,    eng->LTFTCorrection,          DBG_SMOOTH_A);
    float dToe  = dbg_smooth(&smToe,     eng->toeEnrichmentMultiplier, DBG_SMOOTH_A);
    float dRaw  = dbg_smooth(&smRawLoad, (float)rawFuelLoad,           DBG_SMOOTH_A);
    float dPred = dbg_smooth(&smPredLoad, predictedLoad,              DBG_SMOOTH_A);
    float dFlow = dbg_smooth(&smFlow,    (float)flowPerMinTh,          DBG_SMOOTH_A);
    float dDens = dbg_smooth(&smDens,    (float)density,               DBG_SMOOTH_A);
    float dAFlw = dbg_smooth(&smAirFlow, (float)realAirFlow,           DBG_SMOOTH_A);
    float dAMass= dbg_smooth(&smAirMass, (float)realAirMass,           DBG_SMOOTH_A);
    float dAge  = dbg_smooth(&smAge,     (float)stepAgeMs,             DBG_SMOOTH_A);
    float dStep = dbg_smooth(&smStftStep, stftStep,                   DBG_SMOOTH_A);
    float dHead = dbg_smooth(&smHead,    stftHead,                    DBG_SMOOTH_A);
    float dColdM= dbg_smooth(&smColdM,   coldMult,                     DBG_SMOOTH_A);
    float dCrankM=dbg_smooth(&smCrankM,  crankMult,                    DBG_SMOOTH_A);
    float dTrimM= dbg_smooth(&smTrimM,   trimMult,                     DBG_SMOOTH_A);
    float dToeM = dbg_smooth(&smToeM,    toeMult,                      DBG_SMOOTH_A);
    float dStftM= dbg_smooth(&smStftM,   stftMult,                     DBG_SMOOTH_A);
    float dLtftM= dbg_smooth(&smLtftM,   ltftMult,                     DBG_SMOOTH_A);
    float dLtftC= dbg_smooth(&smLtftCell, s.ltft[ltftCell],            DBG_SMOOTH_A);
    float dVeLo = dbg_smooth(&smVeLo,    veLo,                         DBG_SMOOTH_A);
    float dVeHi = dbg_smooth(&smVeHi,    veHi,                         DBG_SMOOTH_A);
    float dVeR  = dbg_smooth(&smVeRatio, veRatio,                      DBG_SMOOTH_A);
    float dCCold= dbg_smooth(&smCCold,   cCold,                        DBG_SMOOTH_A);
    float dCCrank=dbg_smooth(&smCCrank,  cCrank,                       DBG_SMOOTH_A);
    float dCTrim= dbg_smooth(&smCTrim,   cTrim,                        DBG_SMOOTH_A);
    float dCToe = dbg_smooth(&smCToe,    cToe,                         DBG_SMOOTH_A);
    float dCStft= dbg_smooth(&smCStft,   cStft,                        DBG_SMOOTH_A);
    float dCLtft= dbg_smooth(&smCLtft,   cLtft,                        DBG_SMOOTH_A);

    char barTPS[48], barRPM[48], barMAP[48], barVE[48], barLoad[48], barAge[48];
    tui_hbar(barTPS,  sizeof(barTPS),  (dTPS > 100.0f) ? 1.0f : dTPS / 100.0f, 12);
    tui_hbar(barRPM,  sizeof(barRPM),  dRPM / 6000.0f, 12);
    tui_hbar(barMAP,  sizeof(barMAP),  (dAAP > 0.0f) ? dMAP / dAAP : 0.0f, 12);
    tui_hbar(barVE,   sizeof(barVE),   dVE / 110.0f, 12);
    tui_hbar(barLoad, sizeof(barLoad), dLoad / (float)maxFuelLoad, 12);
    tui_hbar(barAge,  sizeof(barAge),  dAge / (float)(loopTime * 2), 12);

    const char *tpsColor = (eng->TPS > 100) ? C_RED : C_GREEN;
    const char *afrColor = (fabsf(afrError) > 3.0f) ? C_RED : (fabsf(afrError) > 1.0f) ? C_YELLOW : C_GREEN;
    const char *ageColor = overdue ? C_RED : (stepAgeMs > loopTime / 2) ? C_YELLOW : C_GREEN;
    const char *learnC = learning ? C_YELLOW C_BOLD : C_DIM;
    const char *ltftOnC = ltftApplied ? C_GREEN C_BOLD : C_DIM;

    float hz = 1.0f / dtSec;
    if (hz > 99.0f) hz = 99.0f;
    float upSec = (firstCaptureMs > 0) ? (float)(now - firstCaptureMs) / 1000.0f : 0.0f;
    float loopHz = (upSec > 0.05f) ? ((float)captureCount / upSec) : 0.0f;
    float warmSec = (float)warmMs / 1000.0f;
    float warmPct = (upSec > 0.05f) ? (warmSec / upSec) * 100.0f : 0.0f;
    float ltftAgo = (lastLtftWriteMs > 0) ? (float)(now - lastLtftWriteMs) / 1000.0f : -1.0f;

    printf("\e[?25l\e[H\e[J");
    tui_top();
    tui_banner(hz, loopHz, upSec);

    tui_section("MODE");
    tui_line("  %s%s RUN%s   %s%s CRANK%s   %s%s COLD%s   %s%s TOE%s   %s%s LEARN%s   %s%s LTFT%s",
             (!eng->EngineCranking && !eng->Coldstart) ? C_GREEN C_BOLD : C_DIM,
             (!eng->EngineCranking && !eng->Coldstart) ? "●" : "○", C_RESET,
             eng->EngineCranking ? C_YELLOW C_BOLD : C_DIM,
             eng->EngineCranking ? "●" : "○", C_RESET,
             eng->Coldstart ? C_YELLOW C_BOLD : C_DIM,
             eng->Coldstart ? "●" : "○", C_RESET,
             toeActive ? C_CYAN C_BOLD : C_DIM,
             toeActive ? "●" : "○", C_RESET,
             learning ? C_YELLOW C_BOLD : C_DIM,
             learning ? "●" : "○", C_RESET,
             ltftApplied ? C_GREEN C_BOLD : C_DIM,
             ltftApplied ? "●" : "○", C_RESET);
    tui_line("  crank %+5d rpm to %d    cool %+4d F to %d    O2 loop always on",
             crankMargin, CRANKING_RPM, coolMargin, eng->coldCoolant);

    tui_section("SENSORS");
    tui_line("  TPS  %s%s%s %s%5.0f%%%s   RPM  %s%s%s %s%5.0f%s",
             tpsColor, barTPS, C_RESET, tpsColor, dTPS, C_RESET,
             C_CYAN, barRPM, C_RESET, C_BOLD, dRPM, C_RESET);
    tui_line("  MAP  %s%s%s %s%5.0f%s kPa  AAP %s%5.0f%s  vac %5.0f  IAT %5.0f K  COOL %5.0f F",
             C_CYAN, barMAP, C_RESET, C_BOLD, dMAP, C_RESET,
             C_BOLD, dAAP, C_RESET, dVac, dIAT, dCool);
    tui_line("  thr %5.2f   TPS/s %+7.0f  RPM/s %+7.0f  MAP/s %+6.0f  dTPS %+5.0f",
             dThr, dTpsRate, dRpmRate, dMapRate, dDeltaTPS);

    tui_section("TRIM");
    tui_line("  STFT %s%+7.2f%%%s  head %s%+6.2f%s  step %+6.2f  %s%s%s",
             C_BOLD, dSTFT, C_RESET,
             (stftHead > 0.0f) ? C_YELLOW : C_DIM, dHead, C_RESET,
             dStep, learnC, learning ? "LEARN" : "hold ", C_RESET);
    {
        char lastBuf[16];
        if (ltftAgo < 0.0f) {
            snprintf(lastBuf, sizeof(lastBuf), "never");
        } else {
            snprintf(lastBuf, sizeof(lastBuf), "%5.2fs", ltftAgo);
        }
        tui_line("  LTFT %s%+7.2f%%%s  dir %s%4s%s  total x%6.3f  last write %s",
                 C_BOLD, dLTFT, C_RESET,
                 learning ? C_YELLOW C_BOLD : C_DIM, ltftDir, C_RESET,
                 totalTrim, lastBuf);
    }
    tui_line("  AFR  tgt %6.2f  real %6.2f  err %s%+6.2f%s   STFT/s %+6.2f  AFR/s %+6.2f",
             dAfrT, dAfrR, afrColor, dAfrE, C_RESET, dStftRate, dAfrRate);
    tui_line("  updates  STFT %-8llu  LTFT writes %-8llu",
             stftUpdateCount, ltftWriteCount);

    tui_section("TOE");
    tui_line("  dTPS %+5.0f  deadband %d  cand %5.2f  decay %d%%  now %s%5.3f%s  %s%s%s",
             dDeltaTPS, TPSDeadband, toeCand, toeInEnrichmentDecay,
             C_BOLD, dToe, C_RESET,
             toeActive ? C_CYAN C_BOLD : C_DIM,
             toeActive ? "ACTIVE" : "idle  ", C_RESET);

    tui_section("FUEL");
    tui_line("  LOAD %s%s%s %s%5.0f%s g/m   VE %s%s%s %s%3.0f%%%s   fuel/s %+7.0f",
             C_MAGENTA, barLoad, C_RESET, C_BOLD, dLoad, C_RESET,
             C_CYAN, barVE, C_RESET, C_BOLD, dVE, C_RESET,
             dFuelRate);
    tui_line("  air  flow/min %-8.0f  dens %-7.0f  airFlow %-8.0f  airMass %-8.0f",
             dFlow, dDens, dAFlw, dAMass);
    tui_line("  raw %5.0f  ->  out %s%6.1f%s g/m",
             dRaw, C_BOLD, dPred, C_RESET);
    tui_line("  x    cold %5.2f  crank %5.2f  trim %5.2f  toe %5.2f  stft %6.3f  ltft %6.3f",
             dColdM, dCrankM, dTrimM, dToeM, dStftM, dLtftM);
    tui_line("  add  cold%+6.1f  crank%+6.1f  trim%+6.1f  toe%+6.1f  stft%+6.1f  ltft%+6.1f",
             dCCold, dCCrank, dCTrim, dCToe, dCStft, dCLtft);
    if (eng->fuelTrim != 1) {
        tui_line("  manual trim %u", eng->fuelTrim);
    }

    tui_section("MAPS");
    tui_line("  MAP bin %2d  [%4u  %4u  %4u]     RPM bin %2d  [%4u  %4u  %4u]",
             mapBin,
             mapAxis[mapBin], eng->MAP,
             (mapBin < MAP_BINS - 1) ? mapAxis[mapBin + 1] : mapAxis[mapBin],
             rpmBin,
             rpmAxis[rpmBin], eng->RPM,
             (rpmBin < RPM_BINS - 1) ? rpmAxis[rpmBin + 1] : rpmAxis[rpmBin]);
    tui_line("  VE   %5.1f  cells %5.1f -> %5.1f  ratio %4.2f  idx %3d",
             dVE, dVeLo, dVeHi, dVeR, veIndex);
    tui_line("  AFR  %5.2f  rpm+ %5.2f  map+ %5.2f  idx %3d",
             afrHere, afrRpmN, afrMapN, afrIndex);

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

        float dShareTL = dbg_smooth(&smShareTL, shareTL, DBG_SMOOTH_A);
        float dShareTR = dbg_smooth(&smShareTR, shareTR, DBG_SMOOTH_A);
        float dShareBL = dbg_smooth(&smShareBL, shareBL, DBG_SMOOTH_A);
        float dShareBR = dbg_smooth(&smShareBR, shareBR, DBG_SMOOTH_A);

        tui_section("LTFT");
        tui_line("  MAP\\RPM  %5u   %5u   %5u     cell %3d = %+6.2f",
                 LTFTRPMAxis[rpmStart],
                 LTFTRPMAxis[rpmStart + 1],
                 LTFTRPMAxis[rpmStart + 2],
                 ltftCell, dLtftC);

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

        tui_line("  shares  TL %5.2f  TR %5.2f  BL %5.2f  BR %5.2f",
                 dShareTL, dShareTR, dShareBL, dShareBR);
        tui_line("  table   nz %3d/256  mean %+6.2f  min %+6.2f  max %+6.2f",
                 ltftNz, ltftMean, ltftMin, ltftMax);
    }

    tui_section("SCHED");
    tui_line("  step %s%3d%s/%-3d   age %s%s%s %s%4.0f%s/%-2d ms   overdue %s%-3s%s   skip %llu",
             C_BOLD, sched->ECUStep, C_RESET, sched->ECULoopSize,
             ageColor, barAge, C_RESET, ageColor, dAge, C_RESET, loopTime,
             overdue ? C_RED C_BOLD : C_GREEN, overdue ? "YES" : "no", C_RESET,
             skipDebt);
    tui_line("  next   crank %3dms   TPS %3dms   STFT/LTFT %3dms",
             crankDueMs, tpsDueMs, stftDueMs);
    tui_line("  ran    crank %s%s%s   toe %s%s%s   STFT+LTFT %s%s%s",
             s.crankRan ? C_GREEN C_BOLD : C_DIM, s.crankRan ? "●" : "○", C_RESET,
             s.toeRan   ? C_GREEN C_BOLD : C_DIM, s.toeRan   ? "●" : "○", C_RESET,
             s.stftRan  ? C_GREEN C_BOLD : C_DIM, s.stftRan  ? "●" : "○", C_RESET);
    {
        double avgAgeMs = (ageSampleCount > 0)
            ? ((double)ageSumMs / (double)ageSampleCount)
            : 0.0;
        tui_line("  peakAge %s%5lld%s ms   avgAge %5.2f ms   overTarget %llu",
                 C_BOLD, peakAgeMs, C_RESET, avgAgeMs, ageOverTargetCount);
    }

    tui_section("SESSION");
    tui_line("  warm %5.1fs (%4.0f%%)   AFR lean %5.1fs  rich %5.1fs  inband %5.1fs",
             warmSec, warmPct,
             (float)afrLeanMs / 1000.0f,
             (float)afrRichMs / 1000.0f,
             (float)afrInbandMs / 1000.0f);
    tui_line("  peak  TPS %-4u  RPM %-5u  load %-5u  |dTPS| %-4lld  |STFT| %5.1f  |LTFT| %5.1f",
             peakTPS, peakRPM, peakFuelLoad, peakDeltaTPS, peakAbsSTFT, peakAbsLTFT);
    tui_line("  peak |AFR err| %5.2f", peakAfrErr);

    tui_section("TUNE");
    tui_line("  STFT  db ±%-2d  damp %.1f     LTFT  sc %.1f",
             STFTDEADBAND, (float)STFTCorrectionDamper, (float)LTFTSCALAR);
    tui_line("  toe   db %-2d  x%.2f  decay %d%%         cold x%.1f  crank x%.1f",
             TPSDeadband, (float)toeEnrichment, toeInEnrichmentDecay,
             (float)coldStartEnrichment, (float)crankingEnrichment);

    tui_section("ALERTS");
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
    printf("%s  ▶ Ctrl+C to exit%s\n", C_DIM, C_RESET);
    fflush(stdout);

    prevTPS = eng->TPS;
    prevRPM = eng->RPM;
    prevMAP = eng->MAP;
    prevFuelLoad = eng->fuelLoad;
    prevSTFT = eng->STFTCorrection;
    prevREALAFR = eng->REALAFR;
    lastPrintMs = now;
    smReady = 1;
}
