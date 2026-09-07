package com.example.cc1101

import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class SecondActivity : AppCompatActivity() {

    private lateinit var gestureDetector: GestureDetector

    companion object {
        private const val SWIPE_THRESHOLD = 100
        private const val SWIPE_VELOCITY_THRESHOLD = 100
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_second)

        val tvCommands = findViewById<TextView>(R.id.tvCommands)
        tvCommands.text = commandsText

        // Настройка свайпа
        gestureDetector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onFling(
                e1: MotionEvent?,
                e2: MotionEvent,
                velocityX: Float,
                velocityY: Float
            ): Boolean {
                if (e1 == null) return false

                val diffX = e2.x - e1.x
                val diffY = e2.y - e1.y

                // Проверяем, что свайп горизонтальный
                if (Math.abs(diffX) > Math.abs(diffY)) {
                    if (Math.abs(diffX) > SWIPE_THRESHOLD && Math.abs(velocityX) > SWIPE_VELOCITY_THRESHOLD) {
                        if (diffX > 0) {
                            // Свайп вправо - возврат
                            finish()
                            overridePendingTransition(R.anim.slide_in_left, R.anim.slide_out_right)
                            return true
                        }
                    }
                }
                return false
            }
        })

        // ✅ Применяем обработку к ScrollView, а не к TextView
        val scrollView = findViewById<ScrollView>(R.id.scrollView)
        scrollView.setOnTouchListener { _, event ->
            gestureDetector.onTouchEvent(event)
            false  // Возвращаем false, чтобы ScrollView продолжал обрабатывать скроллинг
        }
    }

    private val commandsText = """
        ═══════════════════════════════════════
           📡 CC1101 RADIO CONFIGURATION
        ═══════════════════════════════════════

        setmodulation <mode>
        0=2-FSK, 1=GFSK, 2=ASK/OOK, 3=4-FSK, 4=MSK

        setmhz <frequency>
        Base frequency (default 433.92)
        Bands: 300-348, 387-464, 779-928 MHz

        setdeviation <kHz>
        Frequency deviation, 1.58-380.85 kHz (default 47.60)

        setchannel <0-255>
        Channel number (default 0)

        setchsp <spacing>
        Channel spacing in kHz (default 199.95)

        setrxbw <kHz>
        Receive bandwidth (default 812.50)

        setdrate <kBaud>
        Data rate, 0.02-1621.83 kBaud

        setpa <dBm>
        TX power: -30 -20 -15 -10 -6 0 5 7 10 11 12

        applyradio <MHz> <mod> <rate> <dev> <bw> <dBm>
        Apply complete radio profile

        setsyncmode <0-7>
        Sync-word qualifier mode
        0=No preamble/sync
        1=16 sync word bits detected
        2=16/16 sync word bits detected
        3=30/32 sync word bits detected
        4=No preamble/sync, carrier-sense
        5=15/16 + carrier-sense
        6=16/16 + carrier-sense
        7=30/32 + carrier-sense

        setsyncword <LOW HIGH>
        Sync word (must match TX & RX)

        setadrchk <0-3>
        Address check configuration
        0=No address check
        1=Address check, no broadcast
        2=Address check + 0x00 broadcast
        3=Address check + 0x00 & 0xFF broadcast

        setaddr <address>
        Address for packet filtration
        Broadcast: 0x00 / 0xFF

        setwhitedata <0/1>
        Data whitening off/on

        setpktformat <0-3>
        RX/TX data format
        0=Normal mode (FIFO)
        1=Synchronous serial
        2=Random TX mode (PN9)
        3=Asynchronous serial

        setlengthconfig <0-3>
        0=fixed, 1=variable, 2=infinite, 3=Reserved

        setpacketlength <n>
        Packet length (fixed mode) or max (variable)

        setcrc <0/1>
        CRC calculation/check off/on

        setcrcaf <0/1>
        Auto flush RX FIFO on CRC error

        setdcfilteroff <0/1>
        Digital DC blocking filter
        (only for data rates ≤ 250 kBaud)

        setmanchester <0/1>
        Manchester encoding/decoding off/on

        setfec <0/1>
        Forward Error Correction off/on
        (fixed length only)

        setpre <0-7>
        Minimum preamble bytes
        0:2, 1:3, 2:4, 3:6, 4:8, 5:12, 6:16, 7:24

        setpqt <mode>
        Preamble quality estimator threshold

        setappendstatus <0/1>
        Append RSSI/LQI status bytes

        getrssi
        Show radio quality info for last frame

        ═══════════════════════════════════════
              📋 ACTIONS
        ═══════════════════════════════════════

        scan <start> <end>
        Scan frequency range for strongest signal

        rx
        Enable/disable printing of received packets

        tx <hex-vals>
        Send packet of hex values (max 60 bytes)

        jam
        Enable/disable continuous jamming

        brute <usec> <bits>
        Brute force garage gate

        chat
        Switch to IRC-like chat mode

        x
        Stop jamming/receiving/recording

        init
        Restart CC1101 with default parameters

        ═══════════════════════════════════════
          💾 FRAME RECORD / REPLAY
        ═══════════════════════════════════════

        rec
        Enable/disable recording received frames

        show
        Show contents of recording buffer

        add <hex-vals>
        Manually add frame payload (max 60 bytes)

        flush
        Clear recording buffer

        play <N>
        Replay 0=all frames or N-th frame

        save
        Store recording buffer in non-volatile memory

        load
        Load recording buffer from non-volatile memory

        ═══════════════════════════════════════
          🔴 RAW RECORD / REPLAY
        ═══════════════════════════════════════

        rxraw <usec>
        Sniff radio with sampling interval

        addraw <hex-vals>
        Manually add RAW chunks (max 60 bytes)

        recraw <usec>
        Record RAW RF data

        playraw <usec>
        Replay recorded RAW RF data

        showraw
        Show recording buffer in RAW hex

        showbit
        Show recording buffer as bit stream

        echo <0/1>
        Enable/disable command echo

        ═══════════════════════════════════════
        RAW bit order matches URH format
        Run init after any RAW operation
        ═══════════════════════════════════════

        ← Swipe right to go back →""".trimIndent()

}