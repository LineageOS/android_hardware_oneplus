/*
 * Copyright (C) 2021-2023 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.app.NotificationManager
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.media.AudioManager
import android.media.AudioSystem
import android.os.IBinder
import android.os.UEventObserver
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.Settings
import androidx.preference.PreferenceManager

import java.util.concurrent.Executors

class KeyHandler : Service() {
    private lateinit var audioManager: AudioManager
    private lateinit var notificationManager: NotificationManager
    private lateinit var vibrator: Vibrator
    private lateinit var sharedPreferences: SharedPreferences

    private val executorService = Executors.newSingleThreadExecutor()

    private var wasMuted = false
    private val broadcastReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val stream = intent.getIntExtra(AudioManager.EXTRA_VOLUME_STREAM_TYPE, -1)
            val state = intent.getBooleanExtra(AudioManager.EXTRA_STREAM_VOLUME_MUTED, false)
            if (stream == AudioSystem.STREAM_MUSIC && !state) {
                wasMuted = false
            }
        }
    }

    private val alertSliderEventObserver = object : UEventObserver() {
        private val lock = Any()

        override fun onUEvent(event: UEvent) {
            synchronized(lock) {
                event.get("SWITCH_STATE")?.let {
                    handleMode(it.toInt())
                    return
                }
                event.get("STATE")?.let {
                    val none = it.contains("USB=0")
                    val vibration = it.contains("HOST=0")
                    val silent = it.contains("null)=0")

                    if (none && !vibration && !silent) {
                        handleMode(POSITION_BOTTOM)
                    } else if (!none && vibration && !silent) {
                        handleMode(POSITION_MIDDLE)
                    } else if (!none && !vibration && silent) {
                        handleMode(POSITION_TOP)
                    }

                    return
                }
            }
        }
    }

    override fun onCreate() {
        audioManager = getSystemService(AudioManager::class.java)!!
        notificationManager = getSystemService(NotificationManager::class.java)!!
        vibrator = getSystemService(Vibrator::class.java)!!
        sharedPreferences = PreferenceManager.getDefaultSharedPreferences(this)

        registerReceiver(
            broadcastReceiver,
            IntentFilter(AudioManager.STREAM_MUTE_CHANGED_ACTION)
        )
        alertSliderEventObserver.startObserving("tri-state-key")
        alertSliderEventObserver.startObserving("tri_state_key")
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun vibrateIfNeeded(mode: Int) {
        when (mode) {
            AudioManager.RINGER_MODE_VIBRATE -> vibrator.vibrate(
                MODE_VIBRATION_EFFECT,
                HARDWARE_FEEDBACK_VIBRATION_ATTRIBUTES
            )
            AudioManager.RINGER_MODE_NORMAL -> vibrator.vibrate(
                MODE_NORMAL_EFFECT,
                HARDWARE_FEEDBACK_VIBRATION_ATTRIBUTES
            )
        }
    }

    private fun handleMode(position: Int) {
        val muteMedia = sharedPreferences.getBoolean(MUTE_MEDIA_WITH_SILENT, false)

        val mode = when (position) {
            POSITION_TOP -> sharedPreferences.getString(ALERT_SLIDER_TOP_KEY, "0")!!.toInt()
            POSITION_MIDDLE -> sharedPreferences.getString(ALERT_SLIDER_MIDDLE_KEY, "1")!!.toInt()
            POSITION_BOTTOM -> sharedPreferences.getString(ALERT_SLIDER_BOTTOM_KEY, "2")!!.toInt()
            else -> return
        }

        val display = sharedPreferences.getString(ALERT_SLIDER_NOTIFICATION, "2")!!.toInt()

        executorService.submit {
            when (mode) {
                AudioManager.RINGER_MODE_SILENT -> {
                    setZenMode(Settings.Global.ZEN_MODE_OFF)
                    audioManager.ringerModeInternal = mode
                    if (muteMedia) {
                        audioManager.adjustVolume(AudioManager.ADJUST_MUTE, 0)
                        wasMuted = true
                    }
                }
                AudioManager.RINGER_MODE_VIBRATE, AudioManager.RINGER_MODE_NORMAL -> {
                    setZenMode(Settings.Global.ZEN_MODE_OFF)
                    audioManager.ringerModeInternal = mode
                    if (muteMedia && wasMuted) {
                        audioManager.adjustVolume(AudioManager.ADJUST_UNMUTE, 0)
                    }
                }
                ZEN_PRIORITY_ONLY, ZEN_TOTAL_SILENCE, ZEN_ALARMS_ONLY -> {
                    audioManager.ringerModeInternal = AudioManager.RINGER_MODE_NORMAL
                    setZenMode(mode - ZEN_OFFSET)
                    if (muteMedia && wasMuted) {
                        audioManager.adjustVolume(AudioManager.ADJUST_UNMUTE, 0)
                    }
                }
            }
            vibrateIfNeeded(mode)
            sendNotification(position, mode, display)
        }
    }

    private fun setZenMode(zenMode: Int) {
        // Set zen mode
        notificationManager.setZenMode(zenMode, null, TAG)

        // Wait until zen mode change is committed
        while (notificationManager.zenMode != zenMode) {
            Thread.sleep(10)
        }
    }

    private fun sendNotification(position: Int, mode: Int, display: Int) {
        sendBroadcast(Intent(CHANGED_ACTION).apply {
            putExtra("position", position)
            putExtra("mode", mode)
            putExtra("display", display)
        })
    }

    companion object {
        private const val TAG = "KeyHandler"

        // Slider key positions
        const val POSITION_TOP = 1
        const val POSITION_MIDDLE = 2
        const val POSITION_BOTTOM = 3

        // Preference keys
        private const val ALERT_SLIDER_NOTIFICATION = "config_notifications"
        private const val ALERT_SLIDER_TOP_KEY = "config_top_position"
        private const val ALERT_SLIDER_MIDDLE_KEY = "config_middle_position"
        private const val ALERT_SLIDER_BOTTOM_KEY = "config_bottom_position"
        private const val MUTE_MEDIA_WITH_SILENT = "config_mute_media"

        // ZEN constants
        private const val ZEN_OFFSET = 2
        const val ZEN_PRIORITY_ONLY = 3
        const val ZEN_TOTAL_SILENCE = 4
        const val ZEN_ALARMS_ONLY = 5

        // Vibration attributes
        private val HARDWARE_FEEDBACK_VIBRATION_ATTRIBUTES =
            VibrationAttributes.createForUsage(VibrationAttributes.USAGE_HARDWARE_FEEDBACK)

        // Vibration effects
        private val MODE_NORMAL_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_HEAVY_CLICK)
        private val MODE_VIBRATION_EFFECT = VibrationEffect.get(VibrationEffect.EFFECT_DOUBLE_CLICK)

        // Intent actions
        const val CHANGED_ACTION = "org.lineageos.settings.UPDATE_SETTINGS"
    }
}
