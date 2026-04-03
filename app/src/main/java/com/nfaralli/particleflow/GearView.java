package com.nfaralli.particleflow;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

/**
 * Canvas-drawn gear icon. Fully transparent until the user touches
 * the corner it occupies; then fades in and auto-hides after 3 seconds.
 *
 * Reveal it by calling show() from MainActivity.dispatchTouchEvent()
 * when a touch lands in the top-right corner region.
 * Clicking the visible gear fires the OnClickListener.
 */
public class GearView extends View {

    private static final long HIDE_DELAY_MS = 3000;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path  path  = new Path();
    private final Runnable hideRunnable = () -> setAlpha(0f);

    public GearView(Context context) {
        super(context);
        init();
    }

    public GearView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public GearView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        init();
    }

    private void init() {
        paint.setColor(Color.WHITE);
        paint.setStyle(Paint.Style.FILL);
        setAlpha(0f);          // invisible until corner touched
        setClickable(true);
        setFocusable(true);
    }

    /** Call this when a touch lands in the corner area. */
    public void show() {
        removeCallbacks(hideRunnable);
        setAlpha(0.75f);
        postDelayed(hideRunnable, HIDE_DELAY_MS);
    }

    /** Returns true if the gear is currently visible (alpha > 0). */
    public boolean isVisible() {
        return getAlpha() > 0.05f;
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int size = dp(52);
        setMeasuredDimension(size, size);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float cx    = getWidth()  / 2f;
        float cy    = getHeight() / 2f;
        float outer = cx * 0.70f;
        float inner = cx * 0.42f;
        float tooth = cx * 0.22f;
        int   teeth = 8;

        path.reset();
        for (int i = 0; i < teeth; i++) {
            double base0 = Math.PI * 2 * i / teeth;
            double base1 = base0 + Math.PI * 0.45 / teeth;
            double tip0  = base1;
            double tip1  = base0 + Math.PI * 0.95 / teeth;
            double next  = base0 + Math.PI * 1.1  / teeth;

            if (i == 0) path.moveTo(cx + outer * (float)Math.cos(base0),
                                    cy + outer * (float)Math.sin(base0));
            else        path.lineTo(cx + outer * (float)Math.cos(base0),
                                    cy + outer * (float)Math.sin(base0));

            path.lineTo(cx + (outer + tooth) * (float)Math.cos(tip0),
                        cy + (outer + tooth) * (float)Math.sin(tip0));
            path.lineTo(cx + (outer + tooth) * (float)Math.cos(tip1),
                        cy + (outer + tooth) * (float)Math.sin(tip1));
            path.lineTo(cx + outer * (float)Math.cos(next),
                        cy + outer * (float)Math.sin(next));
        }
        path.close();
        path.addCircle(cx, cy, inner, Path.Direction.CCW);

        canvas.drawPath(path, paint);
    }

    private int dp(int dp) {
        return (int)(dp * getResources().getDisplayMetrics().density + 0.5f);
    }
}
