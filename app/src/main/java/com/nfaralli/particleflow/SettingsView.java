package com.nfaralli.particleflow;

import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.Spinner;

/**
 * Settings dialog — inflates settings.xml from master layout.
 * Uses ValidatedEditText, ColorView, GradientView, and Spinner.
 */
public class SettingsView extends Dialog {

    private final VulkanParticlesRenderer renderer;

    // Working copies — only pushed to renderer on Apply
    private int numParticles, particleSize, numTouchPoints;
    private int bgR, bgG, bgB;
    private int slowR, slowG, slowB;
    private int fastR, fastG, fastB;
    private int hueDir, attractionPct, dragPct;

    // Views
    private ValidatedEditText etParticles, etSize, etTouch, etAttract, etDrag;
    private ColorView         cvBg, cvSlow, cvFast;
    private GradientView      bgGradient, particleGradient;
    private Spinner           hueSpinner;

    public SettingsView(Context context, VulkanParticlesRenderer renderer) {
        super(context);
        this.renderer = renderer;
        loadFromRenderer();
    }

    private void loadFromRenderer() {
        numParticles   = renderer.getNumParticles();
        particleSize   = renderer.getParticleSize();
        numTouchPoints = renderer.getNumTouchPoints();
        bgR  = renderer.getBgR();  bgG  = renderer.getBgG();  bgB  = renderer.getBgB();
        slowR= renderer.getSlowR(); slowG= renderer.getSlowG(); slowB= renderer.getSlowB();
        fastR= renderer.getFastR(); fastG= renderer.getFastG(); fastB= renderer.getFastB();
        hueDir       = renderer.getHueDir();
        attractionPct= renderer.getAttractionPct();
        dragPct      = renderer.getDragPct();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.settings);

        etParticles = findViewById(R.id.numParticles);
        etSize      = findViewById(R.id.particleSize);
        etTouch     = findViewById(R.id.numAPoints);
        etAttract   = findViewById(R.id.f01_attraction);
        etDrag      = findViewById(R.id.f01_drag);
        cvBg        = findViewById(R.id.bgColor);
        cvSlow      = findViewById(R.id.slowColor);
        cvFast      = findViewById(R.id.fastColor);
        bgGradient  = findViewById(R.id.bgGradientView);
        particleGradient = findViewById(R.id.gradientView);
        hueSpinner  = findViewById(R.id.hueDirection);

        // Configure ranges
        etParticles.setRange(1, VulkanParticlesRenderer.MAX_PARTICLES);
        etSize.setRange(1, 10);
        etTouch.setRange(1, VulkanParticlesRenderer.MAX_TOUCH_POINTS);
        etAttract.setRange(0, 500);
        etDrag.setRange(0, 999);

        // Populate with current values
        etParticles.setValue(numParticles);
        etSize.setValue(particleSize);
        etTouch.setValue(numTouchPoints);
        etAttract.setValue(attractionPct);
        etDrag.setValue(dragPct);

        cvBg.setColor(bgR, bgG, bgB);
        cvSlow.setColor(slowR, slowG, slowB);
        cvFast.setColor(fastR, fastG, fastB);
        hueSpinner.setSelection(hueDir);

        // Color change listeners — update gradients live
        cvBg.setOnColorChangedListener((r, g, b) -> {
            bgR = r; bgG = g; bgB = b;
            bgGradient.setSolidColor(Color.rgb(r, g, b));
        });
        cvSlow.setOnColorChangedListener((r, g, b) -> {
            slowR = r; slowG = g; slowB = b;
            updateParticleGradient();
        });
        cvFast.setOnColorChangedListener((r, g, b) -> {
            fastR = r; fastG = g; fastB = b;
            updateParticleGradient();
        });

        hueSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                hueDir = pos;
                updateParticleGradient();
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        // Draw initial gradients
        bgGradient.setSolidColor(Color.rgb(bgR, bgG, bgB));
        updateParticleGradient();

        // Buttons
        Button resetBtn  = findViewById(R.id.resetButton);
        Button cancelBtn = findViewById(R.id.cancelButton);
        Button applyBtn  = findViewById(R.id.applyButton);
        resetBtn.setOnClickListener(v -> resetToDefaults());
        cancelBtn.setOnClickListener(v -> dismiss());
        applyBtn.setOnClickListener(v -> applyAndDismiss());
    }

    private void applyAndDismiss() {
        renderer.setNumParticles(etParticles.getValue());
        renderer.setParticleSize(etSize.getValue());
        renderer.setNumTouchPoints(etTouch.getValue());
        renderer.setBgColor(bgR, bgG, bgB);
        renderer.setSlowColor(slowR, slowG, slowB);
        renderer.setFastColor(fastR, fastG, fastB);
        renderer.setHueDir(hueDir);
        renderer.setAttractionPct(etAttract.getValue());
        renderer.setDragPct(etDrag.getValue());
        renderer.savePreferences();
        renderer.applyParameters();
        renderer.resetParticles();
        dismiss();
    }

    private void resetToDefaults() {
        numParticles   = VulkanParticlesRenderer.DEFAULT_NUM_PARTICLES;
        particleSize   = VulkanParticlesRenderer.DEFAULT_PARTICLE_SIZE;
        numTouchPoints = VulkanParticlesRenderer.DEFAULT_NUM_TOUCH;
        bgR = VulkanParticlesRenderer.DEFAULT_BG_R;
        bgG = VulkanParticlesRenderer.DEFAULT_BG_G;
        bgB = VulkanParticlesRenderer.DEFAULT_BG_B;
        slowR = VulkanParticlesRenderer.DEFAULT_SLOW_R;
        slowG = VulkanParticlesRenderer.DEFAULT_SLOW_G;
        slowB = VulkanParticlesRenderer.DEFAULT_SLOW_B;
        fastR = VulkanParticlesRenderer.DEFAULT_FAST_R;
        fastG = VulkanParticlesRenderer.DEFAULT_FAST_G;
        fastB = VulkanParticlesRenderer.DEFAULT_FAST_B;
        hueDir        = VulkanParticlesRenderer.DEFAULT_HUE_DIR;
        attractionPct = VulkanParticlesRenderer.DEFAULT_ATTRACT;
        dragPct       = VulkanParticlesRenderer.DEFAULT_DRAG;

        etParticles.setValue(numParticles);
        etSize.setValue(particleSize);
        etTouch.setValue(numTouchPoints);
        etAttract.setValue(attractionPct);
        etDrag.setValue(dragPct);
        cvBg.setColor(bgR, bgG, bgB);
        cvSlow.setColor(slowR, slowG, slowB);
        cvFast.setColor(fastR, fastG, fastB);
        hueSpinner.setSelection(hueDir);
        bgGradient.setSolidColor(Color.rgb(bgR, bgG, bgB));
        updateParticleGradient();
    }

    private void updateParticleGradient() {
        float[] sh = VulkanParticlesRenderer.rgbToHSV(slowR, slowG, slowB);
        float[] fh = VulkanParticlesRenderer.rgbToHSV(fastR, fastG, fastB);
        int steps = 64;
        int[] colors = new int[steps];
        for (int i = 0; i < steps; i++) {
            float t = i / (float)(steps - 1);
            colors[i] = computeGradientColor(t, sh, fh);
        }
        particleGradient.setColors(colors);
    }

    private int computeGradientColor(float t, float[] sh, float[] fh) {
        float h;
        if (hueDir == 0) {
            // Clockwise: if slow > fast, wrap fast up
            float slowH = sh[0], fastH = fh[0];
            if (slowH > fastH) fastH += 1.0f;
            h = slowH + (fastH - slowH) * t;
            if (h >= 1.0f) h -= 1.0f;
        } else {
            // Counterclockwise: if slow < fast, wrap slow up
            float slowH = sh[0], fastH = fh[0];
            if (slowH < fastH) slowH += 1.0f;
            h = slowH + (fastH - slowH) * t;
            if (h < 0.0f) h += 1.0f;
        }
        float s = sh[1] + (fh[1] - sh[1]) * t;
        float v = sh[2] + (fh[2] - sh[2]) * t;
        float[] rgb = VulkanParticlesRenderer.hsvToRGB(h, s, v);
        return Color.rgb(
            Math.min(255, Math.max(0, (int)(rgb[0] * 255))),
            Math.min(255, Math.max(0, (int)(rgb[1] * 255))),
            Math.min(255, Math.max(0, (int)(rgb[2] * 255)))
        );
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        Window w = getWindow();
        if (w != null) {
            w.setLayout(
                (int)(getContext().getResources().getDisplayMetrics().widthPixels * 0.92f),
                ViewGroup.LayoutParams.WRAP_CONTENT
            );
        }
    }

    private int dp(int dp) {
        return (int)(dp * getContext().getResources().getDisplayMetrics().density + 0.5f);
    }
}
