package com.nfaralli.particleflow;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

/**
 * Custom gear icon view. Draws a simple gear shape on canvas.
 * Sized to ~40dp. Set an OnClickListener from the activity to handle taps.
 */
public class GearView extends View {

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path  path  = new Path();

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
        setAlpha(0.7f);
        setClickable(true);
        setFocusable(true);
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int size = dp(48);
        setMeasuredDimension(size, size);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float cx    = getWidth()  / 2f;
        float cy    = getHeight() / 2f;
        float outer = cx * 0.72f;
        float inner = cx * 0.44f;
        float tooth = cx * 0.20f;
        int   teeth = 8;

        path.reset();
        for (int i = 0; i < teeth; i++) {
            double base0  = Math.PI * 2 * i / teeth;
            double base1  = base0 + Math.PI * 0.5 / teeth;
            double tip0   = base1;
            double tip1   = base0 + Math.PI / teeth;

            float ox0 = cx + outer * (float) Math.cos(base0);
            float oy0 = cy + outer * (float) Math.sin(base0);
            float tx0 = cx + (outer + tooth) * (float) Math.cos(tip0);
            float ty0 = cy + (outer + tooth) * (float) Math.sin(tip0);
            float tx1 = cx + (outer + tooth) * (float) Math.cos(tip1);
            float ty1 = cy + (outer + tooth) * (float) Math.sin(tip1);
            float ox1 = cx + outer * (float) Math.cos(base1 + Math.PI / teeth);
            float oy1 = cy + outer * (float) Math.sin(base1 + Math.PI / teeth);

            if (i == 0) path.moveTo(ox0, oy0);
            else        path.lineTo(ox0, oy0);
            path.lineTo(tx0, ty0);
            path.lineTo(tx1, ty1);
            path.lineTo(ox1, oy1);
        }
        path.close();

        // Cut out centre hole
        path.addCircle(cx, cy, inner, Path.Direction.CCW);

        paint.setColor(Color.WHITE);
        canvas.drawPath(path, paint);
    }

    private int dp(int dp) {
        return (int)(dp * getResources().getDisplayMetrics().density + 0.5f);
    }
}
