package com.nfaralli.particleflow;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.util.Log;
import android.view.Surface;

/**
 * VulkanParticlesRenderer.java
 * Particle Flow — Vulkan Migration
 * NEXUS Engineering — Canyon (Kaneon)
 *
 * Replaces original ParticlesRenderer.java
 * Connects Java UI layer to C++ Vulkan engine via JNI
 * Zero CPU readback — all particle data stays on GPU
 */
public class VulkanParticlesRenderer {

    private static final String TAG = "ParticleFlow";

    // ── PREFERENCES KEYS (same as original app) ──────────────
    public static final String PREFS_NAME        = "ParticleFlowPrefs";
    public static final String KEY_NUM_PARTICLES  = "numParticles";
    public static final String KEY_PARTICLE_SIZE  = "particleSize";
    public static final String KEY_NUM_TOUCH      = "numTouchPoints";
    public static final String KEY_BG_R           = "bgR";
    public static final String KEY_BG_G           = "bgG";
    public static final String KEY_BG_B           = "bgB";
    public static final String KEY_SLOW_R         = "slowR";
    public static final String KEY_SLOW_G         = "slowG";
    public static final String KEY_SLOW_B         = "slowB";
    public static final String KEY_FAST_R         = "fastR";
    public static final String KEY_FAST_G         = "fastG";
    public static final String KEY_FAST_B         = "fastB";
    public static final String KEY_HUE_DIR        = "hueDir";
    public static final String KEY_ATTRACT        = "attraction";
    public static final String KEY_DRAG           = "drag";

    // ── DEFAULTS (confirmed from original particleflow.rs source) ─
    public static final int   DEFAULT_NUM_PARTICLES = 1500000;
    public static final int   DEFAULT_PARTICLE_SIZE = 1;
    public static final int   DEFAULT_NUM_TOUCH      = 6;
    public static final int   DEFAULT_BG_R           = 0;
    public static final int   DEFAULT_BG_G           = 0;
    public static final int   DEFAULT_BG_B           = 0;
    public static final int   DEFAULT_SLOW_R         = 76;   // #4C4CFF
    public static final int   DEFAULT_SLOW_G         = 76;
    public static final int   DEFAULT_SLOW_B         = 255;
    public static final int   DEFAULT_FAST_R         = 255;  // #FF4C4C
    public static final int   DEFAULT_FAST_G         = 76;
    public static final int   DEFAULT_FAST_B         = 76;
    public static final int   DEFAULT_HUE_DIR        = 0;
    public static final int   DEFAULT_ATTRACT        = 100;
    public static final int   DEFAULT_DRAG           = 4;    // 4% → 0.96 drag coef
    public static final int   MAX_PARTICLES          = 4000000;
    public static final int   MAX_TOUCH_POINTS       = 16;

    // ── STATE ─────────────────────────────────────────────────
    // nativeHandle is created/destroyed per Surface (see initialize()/destroy()).
    // All GPU work happens on the render thread; UI-thread calls only post requests (thread-safe).
    private volatile long    nativeHandle = 0;
    private Context          context;
    private volatile int     width  = 0;
    private volatile int     height = 0;
    private volatile boolean initialized = false;
    private int frameCount = 0;   // render thread only (breadcrumbs)
    private android.content.res.AssetManager assets;   // must outlive the native engine

    // Current settings
    private int   numParticles;
    private int   particleSize;
    private int   numTouchPoints;
    private int   bgR, bgG, bgB;
    private int   slowR, slowG, slowB;
    private int   fastR, fastG, fastB;
    private int   hueDir;
    private int   attractionPct;
    private int   dragPct;

    // Touch tracking — written by the UI thread, flushed by the render thread (touchLock).
    // Same semantics as the original app: a point stays where it was until its pointer id has been
    // missing from 3+ consecutive touch events; lifting the last finger does NOT clear anything.
    private final Object  touchLock    = new Object();
    private final float[] touchX       = new float[MAX_TOUCH_POINTS];
    private final float[] touchY       = new float[MAX_TOUCH_POINTS];
    private final boolean[] touchChanged = new boolean[MAX_TOUCH_POINTS];
    private final boolean[] touchCleared = new boolean[MAX_TOUCH_POINTS];
    private final int[]   touchMiss    = new int[MAX_TOUCH_POINTS];

    static {
        System.loadLibrary("particleflow");
    }

    // ── CONSTRUCTOR ───────────────────────────────────────────

    public VulkanParticlesRenderer(Context context) {
        this.context = context;
        loadPreferences();
    }

    // ── LIFECYCLE ─────────────────────────────────────────────

    /** Called on the render thread. Creates a fresh native engine for this Surface. */
    public boolean initialize(Surface surface) {
        String tracePath = CrashReport.tracePath();
        if (tracePath != null) nativeSetTracePath(tracePath);   // native breadcrumbs -> same file
        CrashReport.trace("initialize(): begin");

        if (nativeHandle != 0) destroy();          // stale engine from an earlier surface

        CrashReport.trace("initialize(): nativeCreate");
        long h = nativeCreate();
        if (h == 0) return false;
        nativeHandle = h;
        frameCount = 0;

        // The engine loads its SPIR-V shaders from assets/; without this it has no way to read them.
        assets = context.getAssets();            // keep a strong reference for the native pointer
        nativeSetAssetManager(h, assets);

        // IMPORTANT: parameters first. nativeInitialize() sizes the GPU buffers from the particle
        // count, so it must already know the real value (not the engine's built-in default).
        CrashReport.trace("initialize(): applyParameters, particles=" + numParticles
                + " touch=" + numTouchPoints + " size=" + particleSize);
        applyParameters();

        CrashReport.trace("initialize(): nativeInitialize begin");
        boolean ok = nativeInitialize(h, surface);
        CrashReport.trace("initialize(): nativeInitialize returned " + ok);
        if (!ok) {
            nativeHandle = 0;
            nativeDestroy(h);
            CrashReport.trace("initialize(): failed engine destroyed");
            return false;
        }
        nativeInitParticles(h);
        initialized = true;
        Log.i(TAG, "Renderer initialized: " + numParticles + " particles");
        return true;
    }

    /** Call only after the render thread has stopped. */
    public void destroy() {
        initialized = false;
        long h = nativeHandle;
        nativeHandle = 0;
        if (h != 0) nativeDestroy(h);
    }

    public void onSurfaceChanged(int w, int h) {
        width  = w;
        height = h;
        long handle = nativeHandle;
        if (handle != 0) {
            nativeOnSurfaceChanged(handle, w, h);   // only raises a flag natively
        }
    }

    // ── FRAME LOOP ────────────────────────────────────────────

    /**
     * Called every frame from the render thread.
     * 1. Flush any pending touch changes to the GPU (staging buffer pattern — FIX 5)
     * 2. Dispatch compute shader (physics update)
     * 3. Render particles
     */
    public void drawFrame() {
        long handle = nativeHandle;
        if (!initialized || handle == 0) return;

        // Forward changed touch points (render thread only). The native side uploads them to the
        // GPU inside the frame's own command buffer, so there is no CPU/GPU race.
        synchronized (touchLock) {
            for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
                if (touchChanged[i]) {
                    touchChanged[i] = false;
                    nativeSetTouch(handle, i, touchX[i], touchY[i]);
                }
            }
        }
        // One call per frame: compute step + draw + present (native).
        frameCount++;
        boolean tr = frameCount <= 3;
        if (tr) CrashReport.trace("drawFrame " + frameCount + ": nativeRender begin");
        nativeRender(handle);
        if (tr) CrashReport.trace("drawFrame " + frameCount + ": nativeRender done");
        else if (frameCount == 60 || frameCount == 600 || frameCount == 3000)
            CrashReport.trace("drawFrame " + frameCount + ": still running");
    }

    // ── TOUCH HANDLING ────────────────────────────────────────

    /**
     * Activate or move a touch point. Index = pointer ID.
     * Y is flipped from Android screen space (y=0 top) to particle space (y=0 bottom),
     * matching the original app: touchPos[y] = mHeight - rawY.
     * Actual GPU upload is deferred to the render thread (drawFrame → nativeSetTouch loop).
     */
    public void setTouchPoint(int index, float x, float y) {
        if (index < 0 || index >= numTouchPoints) return;
        synchronized (touchLock) {
            touchX[index] = x;
            touchY[index] = height > 0 ? (height - y) : y;  // flip Y to particle coords
            touchMiss[index]    = 0;
            touchCleared[index] = false;
            touchChanged[index] = true;
        }
    }

    /**
     * Called once per touch event with a bitmask of the pointer ids that are present in it.
     * Ids that are absent get a miss counter; after 3 misses the attraction point is disabled
     * (exactly what the original app did). Never-touched ids (the initial ring) go the same way.
     */
    public void touchesMissed(int presentMask) {
        synchronized (touchLock) {
            for (int i = 0; i < numTouchPoints && i < MAX_TOUCH_POINTS; i++) {
                if ((presentMask & (1 << i)) != 0) continue;
                int c = touchMiss[i];
                if (c < 1000) touchMiss[i] = c + 1;
                if (c >= 3 && !touchCleared[i]) {
                    touchX[i] = -1.0f;
                    touchY[i] = -1.0f;
                    touchCleared[i] = true;
                    touchChanged[i] = true;
                }
            }
        }
    }

    /** Immediately disable a point (not used by the default touch handling). */
    public void releaseTouchPoint(int index) {
        if (index < 0 || index >= numTouchPoints) return;
        synchronized (touchLock) {
            touchX[index] = -1.0f;
            touchY[index] = -1.0f;
            touchCleared[index] = true;
            touchChanged[index] = true;
        }
    }

    // ── TESLA 369 ─────────────────────────────────────────────

    public void initTesla369() {
        long h = nativeHandle;
        if (h != 0) {
            numParticles  = 432000;
            attractionPct = 28;
            dragPct       = 0;
            applyParameters();
            nativeInitTesla369Pattern(h);
            nativeInitParticles(h);
        }
    }

    // ── SETTINGS ─────────────────────────────────────────────

    public void applyParameters() {
        long h = nativeHandle;
        if (h == 0) return;     // not created yet: values are applied in initialize()

        // Convert RGB to HSV for slow color
        float[] slowHSV = rgbToHSV(slowR, slowG, slowB);
        float[] fastHSV = rgbToHSV(fastR, fastG, fastB);

        // Original RenderScript: *d *= f01DragCoef — 1.0 = no drag, 0.0 = full drag (velocity zeroed).
        // UI drag = 0 → dragCoef = 1.0 (preserve velocity). UI drag = 100 → dragCoef = 0.0 (stop).
        // Clamp to 100 so UI values above 100 never produce a negative coefficient.
        float dragCoef = 1.0f - (Math.min(dragPct, 100) / 100.0f);

        // Attraction: use directly (0-500 scale)
        float attractCoef = attractionPct;

        int bgColor = Color.rgb(bgR, bgG, bgB);

        nativeSetParameters(h,
            slowHSV[0], slowHSV[1], slowHSV[2],
            fastHSV[0], fastHSV[1], fastHSV[2],
            hueDir,
            attractCoef,
            dragCoef,
            width, height,
            numParticles,
            numTouchPoints,
            particleSize,
            bgColor
        );
    }

    public void resetParticles() {
        long h = nativeHandle;
        if (h != 0) {
            applyParameters();
            nativeInitParticles(h);   // only raises a flag natively; runs on the render thread
        }
    }

    // ── PREFERENCES ───────────────────────────────────────────

    public void loadPreferences() {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        numParticles  = prefs.getInt(KEY_NUM_PARTICLES, DEFAULT_NUM_PARTICLES);
        particleSize  = prefs.getInt(KEY_PARTICLE_SIZE, DEFAULT_PARTICLE_SIZE);
        numTouchPoints= prefs.getInt(KEY_NUM_TOUCH,     DEFAULT_NUM_TOUCH);
        bgR           = prefs.getInt(KEY_BG_R,          DEFAULT_BG_R);
        bgG           = prefs.getInt(KEY_BG_G,          DEFAULT_BG_G);
        bgB           = prefs.getInt(KEY_BG_B,          DEFAULT_BG_B);
        slowR         = prefs.getInt(KEY_SLOW_R,        DEFAULT_SLOW_R);
        slowG         = prefs.getInt(KEY_SLOW_G,        DEFAULT_SLOW_G);
        slowB         = prefs.getInt(KEY_SLOW_B,        DEFAULT_SLOW_B);
        fastR         = prefs.getInt(KEY_FAST_R,        DEFAULT_FAST_R);
        fastG         = prefs.getInt(KEY_FAST_G,        DEFAULT_FAST_G);
        fastB         = prefs.getInt(KEY_FAST_B,        DEFAULT_FAST_B);
        hueDir        = prefs.getInt(KEY_HUE_DIR,       DEFAULT_HUE_DIR);
        attractionPct = prefs.getInt(KEY_ATTRACT,       DEFAULT_ATTRACT);
        dragPct       = prefs.getInt(KEY_DRAG,          DEFAULT_DRAG);
    }

    public void savePreferences() {
        SharedPreferences.Editor ed = context
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit();
        ed.putInt(KEY_NUM_PARTICLES, numParticles);
        ed.putInt(KEY_PARTICLE_SIZE, particleSize);
        ed.putInt(KEY_NUM_TOUCH,     numTouchPoints);
        ed.putInt(KEY_BG_R,          bgR);
        ed.putInt(KEY_BG_G,          bgG);
        ed.putInt(KEY_BG_B,          bgB);
        ed.putInt(KEY_SLOW_R,        slowR);
        ed.putInt(KEY_SLOW_G,        slowG);
        ed.putInt(KEY_SLOW_B,        slowB);
        ed.putInt(KEY_FAST_R,        fastR);
        ed.putInt(KEY_FAST_G,        fastG);
        ed.putInt(KEY_FAST_B,        fastB);
        ed.putInt(KEY_HUE_DIR,       hueDir);
        ed.putInt(KEY_ATTRACT,       attractionPct);
        ed.putInt(KEY_DRAG,          dragPct);
        ed.apply();
    }

    // ── GETTERS / SETTERS ─────────────────────────────────────

    public int  getNumParticles()   { return numParticles; }
    public int  getParticleSize()   { return particleSize; }
    public int  getNumTouchPoints() { return numTouchPoints; }
    public int  getBgR()            { return bgR; }
    public int  getBgG()            { return bgG; }
    public int  getBgB()            { return bgB; }
    public int  getSlowR()          { return slowR; }
    public int  getSlowG()          { return slowG; }
    public int  getSlowB()          { return slowB; }
    public int  getFastR()          { return fastR; }
    public int  getFastG()          { return fastG; }
    public int  getFastB()          { return fastB; }
    public int  getHueDir()         { return hueDir; }
    public int  getAttractionPct()  { return attractionPct; }
    public int  getDragPct()        { return dragPct; }

    public void setNumParticles(int v)   { numParticles   = Math.min(v, MAX_PARTICLES); }
    public void setParticleSize(int v)   { particleSize   = Math.max(1, v); }
    public void setNumTouchPoints(int v) { numTouchPoints = Math.min(v, MAX_TOUCH_POINTS); }
    public void setBgColor(int r,int g,int b)   { bgR=r; bgG=g; bgB=b; }
    public void setSlowColor(int r,int g,int b) { slowR=r; slowG=g; slowB=b; }
    public void setFastColor(int r,int g,int b) { fastR=r; fastG=g; fastB=b; }
    public void setHueDir(int v)         { hueDir        = v; }
    public void setAttractionPct(int v)  { attractionPct = v; }
    public void setDragPct(int v)        { dragPct       = v; }

    // ── COLOR UTILITIES ───────────────────────────────────────

    /**
     * Convert RGB (0-255) to HSV (0.0-1.0)
     * Matches original app color system exactly
     */
    public static float[] rgbToHSV(int r, int g, int b) {
        float[] hsv = new float[3];
        Color.RGBToHSV(r, g, b, hsv);
        // Android returns H in 0-360, S and V in 0-1
        // Convert H to 0-1 range for shader
        hsv[0] = hsv[0] / 360.0f;
        return hsv;
    }

    /**
     * Compute the gradient color at position t (0.0 to 1.0)
     * between slow color and fast color, using HSV interpolation.
     * Used for the settings gradient preview.
     */
    public int getGradientColor(float t) {
        float[] slowHSV = rgbToHSV(slowR, slowG, slowB);
        float[] fastHSV = rgbToHSV(fastR, fastG, fastB);

        float h, s, v;
        if (hueDir == 0) {
            // Clockwise — increase hue
            float dh = fastHSV[0] - slowHSV[0];
            if (dh < 0) dh += 1.0f;
            h = slowHSV[0] + dh * t;
        } else {
            // Counterclockwise — decrease hue
            float dh = slowHSV[0] - fastHSV[0];
            if (dh < 0) dh += 1.0f;
            h = slowHSV[0] - dh * t;
        }
        h = ((h % 1.0f) + 1.0f) % 1.0f;
        s = slowHSV[1] + (fastHSV[1] - slowHSV[1]) * t;
        v = slowHSV[2] + (fastHSV[2] - slowHSV[2]) * t;

        float[] rgb = hsvToRGB(h, s, v);
        return Color.rgb((int)(rgb[0]*255), (int)(rgb[1]*255), (int)(rgb[2]*255));
    }

    /**
     * HSV to RGB — matches shader implementation exactly
     */
    public static float[] hsvToRGB(float h, float s, float v) {
        float h6 = 6.0f * h;
        float r, g, b;
        if      (h6 < 1) { r=0;     g=1-h6; b=1; }
        else if (h6 < 2) { r=h6-1;  g=0;    b=1; }
        else if (h6 < 3) { r=1;     g=0;    b=3-h6; }
        else if (h6 < 4) { r=1;     g=h6-3; b=0; }
        else if (h6 < 5) { r=5-h6;  g=1;    b=0; }
        else             { r=0;     g=1;    b=h6-5; }
        float coef = v * s;
        return new float[]{ v - coef*r, v - coef*g, v - coef*b };
    }

    // ── JNI BRIDGE ────────────────────────────────────────────
    // All calls go to VulkanRenderer_JNI.cpp → VulkanParticleEngine.cpp

    private native long    nativeCreate();
    private native void    nativeDestroy(long handle);
    private native boolean nativeInitialize(long handle, Surface surface);
    private native void    nativeSetParameters(long handle,
                               float slowH, float slowS, float slowV,
                               float fastH, float fastS, float fastV,
                               int hueDir, float attract, float drag,
                               float w, float h, int numPart, int numTouch,
                               int partSize, int bgColor);
    private native void    nativeInitParticles(long handle);
    private native void    nativeSetTouch(long handle, int index, float x, float y);
    private native void    nativeRender(long handle);
    private native void    nativeOnSurfaceChanged(long handle, int width, int height);
    private native void    nativeResetAttractionPoints(long handle);
    private native void    nativeInitTesla369Pattern(long handle);
    private native void    nativeSetTracePath(String path);
    private native void    nativeSetAssetManager(long handle, android.content.res.AssetManager mgr);
}
