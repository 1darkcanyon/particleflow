package com.nfaralli.particleflow;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.View;

/**
 * Horizontal color gradient preview strip.
 * Call setColors() with a pre-computed array of color stops.
 * For a solid color pass a two-element array with the same color.
 */
public class GradientView extends View {

    private int[]  colors = new int[]{0xFF000000, 0xFF000000};
    private Paint  paint  = new Paint();

    public GradientView(Context context) {
        super(context);
    }

    public GradientView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public GradientView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    /** Set the gradient stops. Minimum 2 colors. */
    public void setColors(int[] colors) {
        if (colors == null || colors.length < 2) return;
        this.colors = colors;
        invalidate();
    }

    /** Convenience: solid color (no gradient). */
    public void setSolidColor(int color) {
        setColors(new int[]{color, color});
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w = getWidth();
        int h = getHeight();
        if (w == 0 || h == 0) return;

        LinearGradient gradient = new LinearGradient(
            0, 0, w, 0,
            colors, null,
            Shader.TileMode.CLAMP
        );
        paint.setShader(gradient);
        canvas.drawRect(0, 0, w, h, paint);
        paint.setShader(null);
    }
}
