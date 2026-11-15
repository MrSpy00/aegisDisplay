package com.aegissoft.aegisdisplay.input

import org.junit.Assert.assertEquals
import org.junit.Test

class PointerInputMapperTest {

    @Test
    fun fitModeHandlesLetterboxMarginsCleanly() {
        // View is 2000x1000, stream is 1000x1000 (1:1).
        // Content occupies x=[500..1500], y=[0..1000].
        val center = PointerInputMapper.mapViewToNormalized(
            viewX = 1000f, viewY = 500f,
            viewWidth = 2000, viewHeight = 1000,
            streamWidth = 1000, streamHeight = 1000,
            scaleMode = ScaleMode.FIT
        )
        assertEquals(32768, center.xNorm)
        assertEquals(32768, center.yNorm)

        val leftBar = PointerInputMapper.mapViewToNormalized(
            viewX = 200f, viewY = 500f,
            viewWidth = 2000, viewHeight = 1000,
            streamWidth = 1000, streamHeight = 1000,
            scaleMode = ScaleMode.FIT
        )
        assertEquals(0, leftBar.xNorm)

        val rightBar = PointerInputMapper.mapViewToNormalized(
            viewX = 1800f, viewY = 500f,
            viewWidth = 2000, viewHeight = 1000,
            streamWidth = 1000, streamHeight = 1000,
            scaleMode = ScaleMode.FIT
        )
        assertEquals(65535, rightBar.xNorm)
    }

    @Test
    fun fillModeMapsCornersWithoutOverflow() {
        val topLeft = PointerInputMapper.mapViewToNormalized(
            viewX = 0f, viewY = 0f,
            viewWidth = 1920, viewHeight = 1080,
            streamWidth = 1920, streamHeight = 1080,
            scaleMode = ScaleMode.FILL
        )
        assertEquals(0, topLeft.xNorm)
        assertEquals(0, topLeft.yNorm)

        val bottomRight = PointerInputMapper.mapViewToNormalized(
            viewX = 1920f, viewY = 1080f,
            viewWidth = 1920, viewHeight = 1080,
            streamWidth = 1920, streamHeight = 1080,
            scaleMode = ScaleMode.FILL
        )
        assertEquals(65535, bottomRight.xNorm)
        assertEquals(65535, bottomRight.yNorm)
    }
}
