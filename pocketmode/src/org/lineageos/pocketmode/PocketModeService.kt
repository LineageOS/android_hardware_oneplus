/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pocketmode

import android.app.Service
import android.content.IntentFilter
import android.content.Intent
import android.os.IBinder
import android.content.BroadcastReceiver
import android.content.Context
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.os.UserHandle
import lineageos.providers.LineageSettings

class PocketModeService : Service() {
    private lateinit var pocketSensor: PocketSensor

    private val screenStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_ON -> onDisplayOn()
                Intent.ACTION_SCREEN_OFF -> onDisplayOff()
            }
        }
    }
    private var screenStateReceiverRegistered = false

    private val settingsObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        fun register() {
            contentResolver.registerContentObserver(
                LineageSettings.System.getUriFor(LineageSettings.System.PROXIMITY_ON_WAKE),
                false,
                this
            )

            onChange(true)
        }

        fun unregister() {
            contentResolver.unregisterContentObserver(this)
        }

        override fun onChange(selfChange: Boolean) {
            val proximityCheckOnWakeEnabledByDefault = resources.getBoolean(
                org.lineageos.platform.internal.R.bool.config_proximityCheckOnWakeEnabledByDefault
            )
            val proximityCheckOnWakeEnabled = LineageSettings.System.getIntForUser(
                contentResolver,
                LineageSettings.System.PROXIMITY_ON_WAKE,
                if (proximityCheckOnWakeEnabledByDefault) 1 else 0,
                UserHandle.USER_CURRENT
            ) == 1

            if (proximityCheckOnWakeEnabled) {
                val screenStateFilter = IntentFilter()
                screenStateFilter.addAction(Intent.ACTION_SCREEN_ON)
                screenStateFilter.addAction(Intent.ACTION_SCREEN_OFF)
                registerReceiver(screenStateReceiver, screenStateFilter)
                screenStateReceiverRegistered = true
            } else if (screenStateReceiverRegistered) {
                unregisterReceiver(screenStateReceiver)
                screenStateReceiverRegistered = false

                pocketSensor.disable()
            }
        }
    }

    override fun onCreate() {
        Log.d(TAG, "Creating service")
        pocketSensor = PocketSensor(this, "oneplus.sensor.pocket")
        settingsObserver.register()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()

        settingsObserver.unregister()
        pocketSensor.disable()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun onDisplayOn() {
        pocketSensor.disable()
    }

    private fun onDisplayOff() {
        pocketSensor.enable()
    }

    companion object {
        private const val TAG = "PocketModeService"
    }
}
