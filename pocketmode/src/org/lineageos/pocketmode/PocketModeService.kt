/*
 * Copyright (c) 2016 The CyanogenMod Project
 *               2021 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.lineageos.pocketmode

import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.UserHandle
import android.util.Log

import lineageos.providers.LineageSettings

class PocketModeService : Service() {
    private var mProximitySensor: ProximitySensor? = null
    private var mSettingsObserver: SettingsObserver? = null

    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate() {
        if (DEBUG) Log.d(TAG, "Creating service")
        mProximitySensor = ProximitySensor(this)
        mSettingsObserver = SettingsObserver(handler)
        mSettingsObserver!!.register()
    }

    override fun onStartCommand(intent: Intent, flags: Int, startId: Int): Int {
        if (DEBUG) Log.d(TAG, "Starting service")
        return START_STICKY
    }

    override fun onDestroy() {
        if (DEBUG) Log.d(TAG, "Destroying service")
        unregisterReceiver(mScreenStateReceiver)
        mProximitySensor?.disable()
        mSettingsObserver!!.unregister()
        super.onDestroy()
    }

    override fun onBind(intent: Intent): IBinder? {
        return null
    }

    private val mScreenStateReceiver: BroadcastReceiver = object
            : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.getAction() == Intent.ACTION_SCREEN_ON) {
                if (DEBUG) Log.d(TAG, "Display on")
                mProximitySensor?.disable()
            } else if (intent.getAction() == Intent.ACTION_SCREEN_OFF) {
                if (DEBUG) Log.d(TAG, "Display off")
                mProximitySensor?.enable()
            }
        }
    }

    private inner class SettingsObserver public constructor(handler: Handler)
            : ContentObserver(handler) {
        private var mIsRegistered = false
        fun register() {
            contentResolver.registerContentObserver(LineageSettings.System.getUriFor(
                    LineageSettings.System.PROXIMITY_ON_WAKE), false, this)
            update()
        }

        fun unregister() {
            contentResolver.unregisterContentObserver(this)
        }

        override fun onChange(selfChange: Boolean) {
            update()
        }

        private fun update() {
            val defaultProximity = resources.getBoolean(
                    org.lineageos.platform.internal.R.bool.config_proximityCheckOnWakeEnabledByDefault)

            val proximityWakeCheckEnabled = LineageSettings.System.getIntForUser(
                    contentResolver, LineageSettings.System.PROXIMITY_ON_WAKE,
                    if (defaultProximity) 1 else 0, UserHandle.USER_CURRENT) == 1

            if (proximityWakeCheckEnabled) {
                val screenStateFilter = IntentFilter(Intent.ACTION_SCREEN_ON)
                screenStateFilter.addAction(Intent.ACTION_SCREEN_OFF)
                registerReceiver(mScreenStateReceiver, screenStateFilter)
                mIsRegistered = true
            } else {
                mProximitySensor?.disable()
                if (mIsRegistered) {
                    unregisterReceiver(mScreenStateReceiver)
                    mIsRegistered = false
                }
            }
        }
    }

    companion object {
        private const val TAG = "PocketModeService"
        private const val DEBUG = false
    }
}
