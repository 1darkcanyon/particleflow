package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;

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

        CrashReport.init(this);
        final String report = CrashReport.consumeReport(this);
        CrashReport.trace("MainActivity.onCreate (report from last run: " + (report != null) + ")");
        if (report != null) {
            // Show what went wrong BEFORE starting the renderer, so a repeat crash cannot hide it.
            showCrashReport(report);
        } else {
            buildUi();
        }
    }

    private void showCrashReport(final String report) {
        android.widget.TextView tv = new android.widget.TextView(this);
        tv.setText(report);
        tv.setTextIsSelectable(true);
        tv.setTypeface(android.graphics.Typeface.MONOSPACE);
        tv.setTextSize(10);
        tv.setPadding(dp(12), dp(8), dp(12), dp(8));
        android.widget.ScrollView sv = new android.widget.ScrollView(this);
        sv.addView(tv);

        final AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("Last run ended badly")
            .setView(sv)
            .setCancelable(false)
            .setPositiveButton("Continue", (d, w) -> buildUi())
            .setNeutralButton("Copy", null)   // listener set below so the dialog stays open
            .show();

        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
            android.content.ClipboardManager cm =
                (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            cm.setPrimaryClip(android.content.ClipData.newPlainText("ParticleFlow crash report", report));
            android.widget.Toast.makeText(this, "Copied - paste it in the chat", android.widget.Toast.LENGTH_LONG).show();
        });
    }

    private void buildUi() {
        try {
            surfaceView = new ParticlesSurfaceView(this);

            // Root frame that layers the surface view with the gear button overlay
            FrameLayout frame = new FrameLayout(this);
            frame.addView(surfaceView);

            // Floating gear button in the top-right corner
            Button gearBtn = new Button(this);
            gearBtn.setText("⚙");
            gearBtn.setTextSize(20);
            gearBtn.setAlpha(0.6f);
            gearBtn.setBackgroundColor(android.graphics.Color.TRANSPARENT);
            gearBtn.setContentDescription(getString(R.string.settings_button_desc));
            int margin = dp(8);
            FrameLayout.LayoutParams gearParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.END
            );
            gearParams.topMargin   = margin;
            gearParams.setMarginEnd(margin);
            gearBtn.setLayoutParams(gearParams);
            gearBtn.setOnClickListener(v -> showSettings());
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
        if (surfaceView != null) {
            SettingsView settings = new SettingsView(this, surfaceView.getRenderer());
            settings.show();
        }
    }

    private int dp(int dp) {
        float density = getResources().getDisplayMetrics().density;
        return (int)(dp * density + 0.5f);
    }
}
