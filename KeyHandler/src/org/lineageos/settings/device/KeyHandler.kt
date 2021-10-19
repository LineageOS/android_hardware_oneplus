/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.app.NotificationManager
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioManager
import android.media.AudioSystem
import android.os.IBinder
import android.os.UEventObserver
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.Settings
import android.view.KeyEvent


class KeyHandler() : Service() {
    private var audioManager: AudioManager? = null
    private var notificationManager: NotificationManager? = null
    private var vibrator: Vibrator? = null
    private var context: Context? = null
    private val sharedPreferences
        get() = this.getSharedPreferences(
            this.packageName + "_preferences",
            Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS
        )

    private var wasMuted = false
    private val broadcastReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val stream = intent?.getIntExtra(AudioManager.EXTRA_VOLUME_STREAM_TYPE, -1)
            val state = intent?.getBooleanExtra(AudioManager.EXTRA_STREAM_VOLUME_MUTED, false)
            if (stream == AudioSystem.STREAM_MUSIC && state == false) {
                wasMuted = false
            }
        }
    }

    private val alertSliderEventObserver = object : UEventObserver() {
        override fun onUEvent(event: UEvent) {
            synchronized(lock) {
                event.get("SWITCH_STATE")?.let {
                    handleKey(it.toInt())
                }
                event.get("STATE")?.let {
                    handleKey(it.toInt())
                }
            }
        }
    }

    override fun onCreate() {
        context = this
        audioManager = this.getSystemService(AudioManager::class.java)
        notificationManager = this.getSystemService(NotificationManager::class.java)
        vibrator = this.getSystemService(Vibrator::class.java)
        alertSliderEventObserver.startObserving("change")
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    init {
        /*context.registerReceiver(
            broadcastReceiver,
            IntentFilter(AudioManager.STREAM_MUTE_CHANGED_ACTION)
        )*/
    }

    fun handleKey(event: Int) {
        when (event) {
            POSITION_TOP -> {
                    val mode = sharedPreferences.getString(ALERT_SLIDER_TOP_KEY, "2")!!.toInt()
                    audioManager?.setRingerModeInternal(mode)
            }
            POSITION_MIDDLE -> {
                    val mode = sharedPreferences.getString(ALERT_SLIDER_MIDDLE_KEY, "1")!!.toInt()
                    audioManager?.setRingerModeInternal(mode)
            }
            POSITION_BOTTOM -> {
                    val mode = sharedPreferences.getString(ALERT_SLIDER_BOTTOM_KEY, "0")!!.toInt()
                    audioManager?.setRingerModeInternal(mode)
            }
        }
    }

    private fun vibrateIfNeeded(mode: Int) {
        when (mode) {
            AudioManager.RINGER_MODE_VIBRATE -> {
                vibrator?.vibrate(MODE_VIBRATION_EFFECT)
            }
            AudioManager.RINGER_MODE_NORMAL -> {
                vibrator?.vibrate(MODE_NORMAL_EFFECT)
            }
        }
    }

    private fun handleMode(mode: Int) {
        val muteMedia = false // sharedPreferences.getBoolean(MUTE_MEDIA_WITH_SILENT, false)

        when (mode) {
            AudioManager.RINGER_MODE_SILENT -> {
                notificationManager?.setZenMode(Settings.Global.ZEN_MODE_OFF, null, TAG)
                audioManager?.setRingerModeInternal(mode)
                if (muteMedia) {
                    audioManager?.adjustVolume(AudioManager.ADJUST_MUTE, 0)
                    wasMuted = true
                }
            }
            AudioManager.RINGER_MODE_VIBRATE, AudioManager.RINGER_MODE_NORMAL -> {
                notificationManager?.setZenMode(Settings.Global.ZEN_MODE_OFF, null, TAG)
                audioManager?.setRingerModeInternal(mode)
                if (muteMedia && wasMuted) {
                    audioManager?.adjustVolume(AudioManager.ADJUST_UNMUTE, 0)
                }
            }
            ZEN_PRIORITY_ONLY, ZEN_TOTAL_SILENCE, ZEN_ALARMS_ONLY -> {
                audioManager?.setRingerModeInternal(AudioManager.RINGER_MODE_NORMAL)
                notificationManager?.setZenMode(mode - ZEN_OFFSET, null, TAG)
                if (muteMedia && wasMuted) {
                    audioManager?.adjustVolume(AudioManager.ADJUST_UNMUTE, 0)
                }
            }
        }
        vibrateIfNeeded(mode)
    }

    companion object {
        private const val TAG = "KeyHandler"

        private val lock = Any()

        // Slider key codes
        private const val POSITION_TOP = 1
        private const val POSITION_MIDDLE = 2
        private const val POSITION_BOTTOM = 3

        // Preference keys
        private const val ALERT_SLIDER_TOP_KEY = "config_top_position"
        private const val ALERT_SLIDER_MIDDLE_KEY = "config_middle_position"
        private const val ALERT_SLIDER_BOTTOM_KEY = "config_bottom_position"
        private const val MUTE_MEDIA_WITH_SILENT = "config_mute_media"

        // ZEN constants
        private const val ZEN_OFFSET = 2
        private const val ZEN_PRIORITY_ONLY = 3
        private const val ZEN_TOTAL_SILENCE = 4
        private const val ZEN_ALARMS_ONLY = 5

        // Vibration effects
        private val MODE_NORMAL_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_HEAVY_CLICK)
        private val MODE_VIBRATION_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_DOUBLE_CLICK)
    }
}
