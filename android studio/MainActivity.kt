package com.example.cc1101

import android.content.Intent
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var gestureDetector: GestureDetector
    private lateinit var webView: WebView

    companion object {
        private const val SWIPE_THRESHOLD = 100
        private const val SWIPE_VELOCITY_THRESHOLD = 100
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        webView = findViewById(R.id.webView)

        webView.apply {
            settings.apply {
                javaScriptEnabled = true
                domStorageEnabled = true
                allowFileAccess = true
                setSupportZoom(true)
                builtInZoomControls = true
                displayZoomControls = false
            }
            webViewClient = WebViewClient()
            loadUrl("http://192.168.1.100/")
        }

        gestureDetector = GestureDetector(this, SwipeGestureListener())

        // ✅ Применяем GestureDetector непосредственно к WebView
        webView.setOnTouchListener { _, event ->
            gestureDetector.onTouchEvent(event)
            // ⚠️ Возвращаем false, чтобы WebView мог обрабатывать свои события
            false
        }
    }

    inner class SwipeGestureListener : GestureDetector.SimpleOnGestureListener() {

        override fun onFling(
            e1: MotionEvent?,
            e2: MotionEvent,
            velocityX: Float,
            velocityY: Float
        ): Boolean {
            if (e1 == null) return false

            val diffX = e2.x - e1.x
            val diffY = e2.y - e1.y

            if (Math.abs(diffX) > Math.abs(diffY)) {
                if (Math.abs(diffX) > SWIPE_THRESHOLD && Math.abs(velocityX) > SWIPE_VELOCITY_THRESHOLD) {
                    when {
                        diffX < 0 -> {
                            // ✅ Свайп влево - SecondActivity (команды)
                            navigateToSecondActivity()
                            return true
                        }

                        diffX > 0 -> {
                            // ✅ Свайп вправо - ThirdActivity (информация)
                            navigateToThirdActivity()
                            return true
                        }
                    }
                }
            }
            return false
        }

        private fun navigateToSecondActivity() {
            val intent = Intent(this@MainActivity, SecondActivity::class.java)
            startActivity(intent)
            overridePendingTransition(R.anim.slide_in_right, R.anim.slide_out_left)
        }
        private fun navigateToThirdActivity() {
            val intent = Intent(this@MainActivity, ThirdActivity::class.java)
            startActivity(intent)
            overridePendingTransition(R.anim.slide_in_left, R.anim.slide_out_right)
        }
    }
}