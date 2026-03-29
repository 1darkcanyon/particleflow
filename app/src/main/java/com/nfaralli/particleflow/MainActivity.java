package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

public class MainActivity extends Activity {

    private ParticlesSurfaceView surfaceView;
    private boolean              mSettingsOpen = false;

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

            // Overlay: particle surface + floating gear button
            FrameLayout frame = new FrameLayout(this);
            frame.addView(surfaceView);

            TextView gearBtn = new TextView(this);
            gearBtn.setText("⚙");
            gearBtn.setTextSize(28f);
            gearBtn.setTextColor(0xCCFFFFFF);
            int pad = Math.round(8 * getResources().getDisplayMetrics().density);
            int padV = Math.round(6 * getResources().getDisplayMetrics().density);
            gearBtn.setPadding(pad, padV, pad, padV);
            gearBtn.setBackgroundColor(0x55000000);
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP | Gravity.END);
            lp.setMargins(0, Math.round(24 * getResources().getDisplayMetrics().density),
                    Math.round(8 * getResources().getDisplayMetrics().density), 0);
            gearBtn.setLayoutParams(lp);
            gearBtn.setOnClickListener(v -> { if (!mSettingsOpen) showSettings(); });
            frame.addView(gearBtn);

            setContentView(frame);
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
        if (surfaceView == null) return;
        mSettingsOpen = true;
        SettingsView settings = new SettingsView(this, surfaceView.getRenderer());
        settings.setOnDismissListener(d -> mSettingsOpen = false);
        settings.show();
    }
}
