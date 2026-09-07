package com.example.cc1101

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.text.SimpleDateFormat
import java.util.*

class ThirdActivity : AppCompatActivity() {

    private lateinit var gestureDetector: GestureDetector
    private lateinit var etInput: EditText
    private lateinit var spinnerAction: Spinner
    private lateinit var btnAction: Button
    private lateinit var btnCopy: Button
    private lateinit var btnClear: Button
    private lateinit var btnSave: Button
    private lateinit var btnClearHistory: Button
    private lateinit var tvOutput: TextView
    private lateinit var tvHistory: TextView
    private lateinit var sharedPreferences: SharedPreferences
    private var selectedAction: String = "Инвертировать"

    companion object {
        private const val SWIPE_THRESHOLD = 50
        private const val SWIPE_VELOCITY_THRESHOLD = 50
        private const val PREFS_NAME = "InvertHistory"
        private const val HISTORY_KEY = "history"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_third)

        sharedPreferences = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        etInput = findViewById(R.id.etInput)
        spinnerAction = findViewById(R.id.spinnerAction)
        btnAction = findViewById(R.id.btnAction)
        btnCopy = findViewById(R.id.btnCopy)
        btnClear = findViewById(R.id.btnClear)
        btnSave = findViewById(R.id.btnSave)
        btnClearHistory = findViewById(R.id.btnClearHistory)
        tvOutput = findViewById(R.id.tvOutput)
        tvHistory = findViewById(R.id.tvHistory)

        // Настройка Spinner
        setupSpinner()

        btnAction.setOnClickListener { processText() }
        btnCopy.setOnClickListener { copyResult() }
        btnClear.setOnClickListener { clearAll() }
        btnSave.setOnClickListener { saveToHistory() }
        btnClearHistory.setOnClickListener { clearHistory() }

        tvHistory.setOnClickListener { showHistoryActions() }
        tvHistory.setOnLongClickListener {
            copyLastEntry()
            true
        }

        showHistory()

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

                if (Math.abs(diffX) > Math.abs(diffY)) {
                    if (Math.abs(diffX) > SWIPE_THRESHOLD && Math.abs(velocityX) > SWIPE_VELOCITY_THRESHOLD) {
                        if (diffX < 0) {
                            finish()
                            overridePendingTransition(R.anim.slide_in_right, R.anim.slide_out_left)
                            return true
                        }
                    }
                }
                return false
            }
        })
    }

    // ✅ Настройка выпадающего списка
    private fun setupSpinner() {
        val actions = arrayOf(
            "🔄 invert",
            "🔢 1",
            "🎯 2",
            "🔤 3",
            "📈 4",
            "📉 5",
            "🔀 6"
        )

        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, actions)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        spinnerAction.adapter = adapter

        spinnerAction.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                selectedAction = actions[position]
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {
                selectedAction = actions[0]
            }
        }
    }

    // ✅ Обработка текста в зависимости от выбранного действия
    private fun processText() {
        val inputText = etInput.text.toString()

        if (inputText.isEmpty()) {
            Toast.makeText(this, "Введите текст!", Toast.LENGTH_SHORT).show()
            return
        }

        val result = when {
            selectedAction.contains("invert") -> {
                inputText.reversed()
            }
            selectedAction.contains("1") -> {
                "Количество символов: ${inputText.length}"
            }
            selectedAction.contains("2") -> {
                // ✅ Только четные символы (индексы: 0, 2, 4, 6...)
                inputText.filterIndexed { index, _ -> index % 2 == 0 }
            }
            selectedAction.contains("3") -> {
                // ✅ Только нечетные символы (индексы: 1, 3, 5, 7...)
                inputText.filterIndexed { index, _ -> index % 2 == 1 }
            }
            selectedAction.contains("4") -> {
                inputText.uppercase(Locale.getDefault())
            }
            selectedAction.contains("5") -> {
                inputText.lowercase(Locale.getDefault())
            }
            selectedAction.contains("6") -> {
                // ✅ Перемешиваем символы случайным образом
                inputText.toCharArray().joinToString("")
            }
            else -> {
                inputText
            }
        }

        tvOutput.text = result
    }

    private fun saveToHistory() {
        val text = tvOutput.text.toString()

        if (text.isEmpty() || text == "Результат будет здесь") {
            Toast.makeText(this, "Сначала обработайте текст!", Toast.LENGTH_SHORT).show()
            return
        }

        val dateFormat = SimpleDateFormat("dd.MM.yy HH:mm:ss", Locale.getDefault())
        val timestamp = dateFormat.format(Date())

        val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""
        val newEntry = "$timestamp | $text\n"
        val updatedHistory = newEntry + history

        sharedPreferences.edit().putString(HISTORY_KEY, updatedHistory).apply()

        Toast.makeText(this, "✅ Сохранено!", Toast.LENGTH_SHORT).show()
        showHistory()

        etInput.text.clear()
        tvOutput.text = "Результат будет здесь"
    }

    private fun copyResult() {
        val result = tvOutput.text.toString()
        if (result.isNotEmpty() && result != "Результат будет здесь") {
            val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText("Processed text", result)
            clipboard.setPrimaryClip(clip)
            Toast.makeText(this, "📋 Текст скопирован!", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Нет текста для копирования!", Toast.LENGTH_SHORT).show()
        }
    }

    private fun clearAll() {
        etInput.text.clear()
        tvOutput.text = "Результат будет здесь"
        Toast.makeText(this, "🧹 Поля очищены", Toast.LENGTH_SHORT).show()
    }

    private fun showHistory() {
        val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""

        if (history.isEmpty()) {
            tvHistory.text = "📭 История пуста\n\nНажмите для добавления"
        } else {
            tvHistory.text = "📜 ИСТОРИЯ (нажмите для действий):\n\n$history"
        }
    }

    private fun clearHistory() {
        AlertDialog.Builder(this)
            .setTitle("🗑️ Очистить историю")
            .setMessage("Вы уверены, что хотите удалить все записи?")
            .setPositiveButton("Да") { _, _ ->
                sharedPreferences.edit().remove(HISTORY_KEY).apply()
                Toast.makeText(this, "История очищена!", Toast.LENGTH_SHORT).show()
                showHistory()
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun showHistoryActions() {
        val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""

        if (history.isEmpty()) {
            Toast.makeText(this, "История пуста!", Toast.LENGTH_SHORT).show()
            return
        }

        val lines = history.split("\n").filter { it.isNotEmpty() }
        if (lines.isEmpty()) {
            Toast.makeText(this, "Нет записей", Toast.LENGTH_SHORT).show()
            return
        }

        val items = lines.toTypedArray()
        val actions = arrayOf("📝 Редактировать", "📋 Копировать", "🗑️ Удалить")

        AlertDialog.Builder(this)
            .setTitle("Выберите запись")
            .setItems(items) { _, which ->
                val selectedEntry = items[which]
                val entryIndex = which

                AlertDialog.Builder(this)
                    .setTitle("Действия с записью")
                    .setItems(actions) { _, actionIndex ->
                        when (actionIndex) {
                            0 -> editEntry(entryIndex, selectedEntry)
                            1 -> copyTextToClipboard(selectedEntry)
                            2 -> deleteEntry(entryIndex)
                        }
                    }
                    .setNegativeButton("Отмена", null)
                    .show()
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun editEntry(index: Int, oldEntry: String) {
        val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""
        val lines = history.split("\n").filter { it.isNotEmpty() }.toMutableList()

        if (index < 0 || index >= lines.size) {
            Toast.makeText(this, "Ошибка!", Toast.LENGTH_SHORT).show()
            return
        }

        val oldText = if (oldEntry.contains("|")) {
            oldEntry.substringAfter("| ").trim()
        } else {
            oldEntry
        }

        val editText = EditText(this)
        editText.setText(oldText)
        editText.setSelection(oldText.length)
        editText.hint = "Введите новый текст"

        AlertDialog.Builder(this)
            .setTitle("✏️ Редактировать запись")
            .setMessage("Измените текст записи:")
            .setView(editText)
            .setPositiveButton("Сохранить") { _, _ ->
                val newText = editText.text.toString().trim()
                if (newText.isEmpty()) {
                    Toast.makeText(this, "Текст не может быть пустым!", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }

                val timestamp = if (oldEntry.contains("|")) {
                    oldEntry.substringBefore("|").trim()
                } else {
                    SimpleDateFormat("dd.MM.yyyy HH:mm:ss", Locale.getDefault()).format(Date())
                }

                val newEntry = "$timestamp | $newText"
                lines[index] = newEntry

                val updatedHistory = lines.joinToString("\n") + "\n"
                sharedPreferences.edit().putString(HISTORY_KEY, updatedHistory).apply()

                Toast.makeText(this, "✅ Запись обновлена!", Toast.LENGTH_SHORT).show()
                showHistory()
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun deleteEntry(index: Int) {
        AlertDialog.Builder(this)
            .setTitle("🗑️ Удалить запись")
            .setMessage("Вы уверены, что хотите удалить эту запись?")
            .setPositiveButton("Да") { _, _ ->
                val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""
                val lines = history.split("\n").filter { it.isNotEmpty() }.toMutableList()

                if (index in lines.indices) {
                    lines.removeAt(index)
                    val updatedHistory = if (lines.isEmpty()) {
                        ""
                    } else {
                        lines.joinToString("\n") + "\n"
                    }
                    sharedPreferences.edit().putString(HISTORY_KEY, updatedHistory).apply()
                    Toast.makeText(this, "🗑️ Запись удалена!", Toast.LENGTH_SHORT).show()
                    showHistory()
                }
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun copyLastEntry() {
        val history = sharedPreferences.getString(HISTORY_KEY, "") ?: ""

        if (history.isEmpty()) {
            Toast.makeText(this, "История пуста!", Toast.LENGTH_SHORT).show()
            return
        }

        val lines = history.split("\n").filter { it.isNotEmpty() }
        if (lines.isEmpty()) {
            Toast.makeText(this, "Нет записей", Toast.LENGTH_SHORT).show()
            return
        }

        copyTextToClipboard(lines.first())
    }

    private fun copyTextToClipboard(entry: String) {
        val textWithoutTimestamp = if (entry.contains("|")) {
            entry.substringAfter("| ").trim()
        } else {
            entry
        }

        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("History entry", textWithoutTimestamp)
        clipboard.setPrimaryClip(clip)

        val displayText = if (textWithoutTimestamp.length > 50) {
            textWithoutTimestamp.take(50) + "..."
        } else {
            textWithoutTimestamp
        }
        Toast.makeText(this, "📋 Скопировано: $displayText", Toast.LENGTH_LONG).show()
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        return super.dispatchTouchEvent(event)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        return super.onTouchEvent(event)
    }
}