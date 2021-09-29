/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.content.ContentResolver
import android.content.Context
import android.database.ContentObserver
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.os.UserHandle
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.KeyEvent
import com.android.internal.os.DeviceKeyHandler

import lineageos.providers.LineageSettings

class KeyHandler(private val context: Context) : DeviceKeyHandler {
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val vibrator = context.getSystemService(Vibrator::class.java)

    val RINGER_MODES: IntArray;

    override fun handleKeyEvent(event: KeyEvent): KeyEvent {
        if (event.action == KeyEvent.ACTION_DOWN) {
            when (event.scanCode) {
                POS_TOP -> {
                    audioManager.setRingerModeInternal(RINGER_MODES[0])
                    vibrator.vibrate(MODE_NORMAL_EFFECT)
                }
                POS_MIDDLE -> {
                    audioManager.setRingerModeInternal(RINGER_MODES[1])
                    vibrator.vibrate(MODE_VIBRATION_EFFECT)
                }
                POS_BOTTOM -> {
                    audioManager.setRingerModeInternal(RINGER_MODES[3])
                }
            }
        }
        return event
    }

    companion object {
        private const val TAG = "KeyHandler"

        // Slider key codes
        private const val POS_TOP = 601
        private const val POS_MIDDLE = 602
        private const val POS_BOTTOM = 603

        // Vibration effects
        private val MODE_NORMAL_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_HEAVY_CLICK)
        private val MODE_VIBRATION_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_DOUBLE_CLICK)
    }

    init {
        // Ringer modes
        val resolver : ContentResolver = context.getContentResolver();
        RINGER_MODES = intArrayOf(
            LineageSettings.System.getInt(resolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_TOP_POS_MODE, 0),
            LineageSettings.System.getInt(resolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_MIDDLE_POS_MODE, 0),
            LineageSettings.System.getInt(resolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_BOTTOM_POS_MODE, 0)
        )
    }

/*    private val settingsObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        fun register() {
            getContentResolver().registerContentObserver(
                LineageSettings.System.getUriFor(LineageSettings.System.KEY_NOTIF_SLIDER_TOP_POS_MODE),
                false,
                this
            )
            getContentResolver().registerContentObserver(
                LineageSettings.System.getUriFor(LineageSettings.System.KEY_NOTIF_SLIDER_MIDDLE_POS_MODE),
                false,
                this
            )
            getContentResolver().registerContentObserver(
                LineageSettings.System.getUriFor(LineageSettings.System.KEY_NOTIF_SLIDER_BOTTOM_POS_MODE),
                false,
                this
            )

            onChange(true)
        }

        fun unregister() {
            contentResolver.unregisterContentObserver(this)
        }

        override fun onChange(selfChange: Boolean) {
            RINGER_MODES[0] = LineageSettings.System.getIntForUser(contentResolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_TOP_POS_MODE, 0,
                UserHandle.USER_CURRENT
            )
            RINGER_MODES[0] = LineageSettings.System.getIntForUser(contentResolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_MIDDLE_POS_MODE, 0,
                UserHandle.USER_CURRENT
            )
            RINGER_MODES[0] = LineageSettings.System.getIntForUser(contentResolver,
                LineageSettings.System.KEY_NOTIF_SLIDER_BOTTOM_POS_MODE, 0,
                UserHandle.USER_CURRENT
            )
        }
    }*/
}
