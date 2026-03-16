package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;

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

        try {
            surfaceView = new ParticlesSurfaceView(this);
            setContentView(surfaceView);
        } catch (Throwable t) {
            // Show error on screen so we can read it
            new AlertDialog.Builder(this)
                .setTitle("NEXUS Crash")
                .setMessage(t.getClass().getSimpleName() + "\n\n" + t.getMessage())
                .setPositiveButton("OK", null)
                .show();
        }
    }

    public void initTesla369() {
        if (surfaceView != null) surfaceView.initTesla369();
    }

    public void showSettings() {
        if (surfaceView != null) {
            SettingsView settings = new SettingsView(this, surfaceView.getRenderer());
            settings.show();
        }
    }
}
