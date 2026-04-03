package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public class MainActivity extends Activity {

    private ParticlesSurfaceView surfaceView;
    private GearView             gearView;

    // Corner hit area — touches within this many px of the top-right corner reveal the gear
    private static final int CORNER_DP = 80;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        );

        try {
            setContentView(R.layout.activity_main);

            surfaceView = findViewById(R.id.particles_view);
            gearView    = findViewById(R.id.gear_view);

            // Tapping the visible gear opens settings
            gearView.setOnClickListener(v -> {
                if (gearView.isVisible()) showSettings();
            });

        } catch (Throwable t) {
            new AlertDialog.Builder(this)
                .setTitle("ParticleFlow Crash")
                .setMessage(t.getClass().getSimpleName() + "\n\n" + t.getMessage())
                .setPositiveButton("OK", null)
                .show();
        }
    }

    /**
     * Intercept every touch to check for corner touches before dispatching normally.
     * This lets the gear reveal without blocking particle simulation touches.
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN && gearView != null) {
            float cornerPx = CORNER_DP * getResources().getDisplayMetrics().density;
            int   screenW  = getWindow().getDecorView().getWidth();
            float x = event.getX();
            float y = event.getY();
            if (x >= screenW - cornerPx && y <= cornerPx) {
                gearView.show();
            }
        }
        return super.dispatchTouchEvent(event);
    }

    public void showSettings() {
        if (surfaceView != null) {
            new SettingsView(this, surfaceView.getRenderer()).show();
        }
    }

    public void initTesla369() {
        if (surfaceView != null) surfaceView.initTesla369();
    }
}
