package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public class MainActivity extends Activity {

    private ParticlesSurfaceView surfaceView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Extend into display cutout (notch) on API 28+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // Immersive sticky: hides status bar and nav bar
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
            GearView gearView = findViewById(R.id.gear_view);
            gearView.setOnClickListener(v -> showSettings());

        } catch (Throwable t) {
            new AlertDialog.Builder(this)
                .setTitle("ParticleFlow Crash")
                .setMessage(t.getClass().getSimpleName() + "\n\n" + t.getMessage())
                .setPositiveButton("OK", null)
                .show();
        }
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
