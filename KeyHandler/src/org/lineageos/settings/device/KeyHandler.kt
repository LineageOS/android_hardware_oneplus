/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.app.NotificationManager
import android.content.Context
import android.content.SharedPreferences
import android.media.AudioManager
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.Settings
import android.view.KeyEvent

import androidx.preference.PreferenceManager

import com.android.internal.os.DeviceKeyHandler

class KeyHandler(private val context: Context) : DeviceKeyHandler {
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val notificationManager = context.getSystemService(NotificationManager::class.java);
    private val vibrator = context.getSystemService(Vibrator::class.java)
    private val packageContext = context.createPackageContext(
            KeyHandler::class.java.getPackage().name, 0)

    override fun handleKeyEvent(event: KeyEvent): KeyEvent {
        if (event.action == KeyEvent.ACTION_DOWN) {
            when (event.scanCode) {
                POSITION_TOP -> {
                    handleMode(Integer.parseInt(packageContext.getSharedPreferences(
                            packageContext.getPackageName() + "_preferences",
                            Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS)
                            .getString(NOTIF_SLIDER_TOP_KEY, "2")))
                }
                POSITION_MIDDLE -> {
                    handleMode(Integer.parseInt(packageContext.getSharedPreferences(
                            packageContext.getPackageName() + "_preferences",
                            Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS)
                            .getString(NOTIF_SLIDER_MIDDLE_KEY, "1")))
                }
                POSITION_BOTTOM -> {
                    handleMode(Integer.parseInt(packageContext.getSharedPreferences(
                            packageContext.getPackageName() + "_preferences",
                            Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS)
                            .getString(NOTIF_SLIDER_BOTTOM_KEY, "0")))
                }
            }
        }
        return event
    }

    fun vibrateIfNeeded(mode: Int) {
        when (mode) {
            1 -> {
                vibrator.vibrate(MODE_VIBRATION_EFFECT)
            }
            2 -> {
                vibrator.vibrate(MODE_NORMAL_EFFECT)
            }
        }
    }

    fun handleMode(mode: Int) {
        when (mode) {
            0, 1, 2 -> {
                notificationManager.setZenMode(Settings.Global.ZEN_MODE_OFF, null, TAG)
                audioManager.setRingerModeInternal(mode)
            }
            3, 4, 5 -> {
                audioManager.setRingerModeInternal(AudioManager.RINGER_MODE_NORMAL);
                notificationManager.setZenMode(mode - 2, null, TAG)
            }
        }
        vibrateIfNeeded(mode)
    }

    companion object {
        private const val TAG = "KeyHandler"

        // Slider key codes
        private const val POSITION_TOP = 601
        private const val POSITION_MIDDLE = 602
        private const val POSITION_BOTTOM = 603

        // Preference keys
        private const val NOTIF_SLIDER_TOP_KEY = "config_top_position"
        private const val NOTIF_SLIDER_MIDDLE_KEY = "config_middle_position"
        private const val NOTIF_SLIDER_BOTTOM_KEY = "config_bottom_position"

        // Vibration effects
        private val MODE_NORMAL_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_HEAVY_CLICK)
        private val MODE_VIBRATION_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_DOUBLE_CLICK)
    }
}
