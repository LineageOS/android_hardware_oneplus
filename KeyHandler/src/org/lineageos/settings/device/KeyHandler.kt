/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.content.Context
import android.content.SharedPreferences
import android.media.AudioManager
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.KeyEvent

import androidx.preference.PreferenceManager

import com.android.internal.os.DeviceKeyHandler

class KeyHandler(context: Context) : DeviceKeyHandler {
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val vibrator = context.getSystemService(Vibrator::class.java)
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)

    override fun handleKeyEvent(event: KeyEvent): KeyEvent {
        if (event.action == KeyEvent.ACTION_DOWN) {
            when (event.scanCode) {
                POSITION_TOP -> {
                    audioManager.setRingerModeInternal(
                            preferences.getInt(NOTIF_SLIDER_TOP_KEY, 0))
                    vibrator.vibrate(MODE_NORMAL_EFFECT)
                }
                POSITION_MIDDLE -> {
                    audioManager.setRingerModeInternal(
                            preferences.getInt(NOTIF_SLIDER_MIDDLE_KEY, 1))
                    vibrator.vibrate(MODE_VIBRATION_EFFECT)
                }
                POSITION_BOTTOM -> {
                    audioManager.setRingerModeInternal(
                            preferences.getInt(NOTIF_SLIDER_BOTTOM_KEY, 2))
                }
            }
        }
        return event
    }

    companion object {
        private const val TAG = "KeyHandler"

        // Slider key codes
        private const val POSITION_TOP = 601
        private const val POSITION_MIDDLE = 602
        private const val POSITION_BOTTOM = 603

        private const val NOTIF_SLIDER_TOP_KEY = "config_top_position";
        private const val NOTIF_SLIDER_MIDDLE_KEY = "config_keycode_middle_position";
        private const val NOTIF_SLIDER_BOTTOM_KEY = "config_keycode_bottom_position";

        // Vibration effects
        private val MODE_NORMAL_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_HEAVY_CLICK)
        private val MODE_VIBRATION_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_DOUBLE_CLICK)
    }
}
