package org.meshmesh.app

import android.content.Context
import org.json.JSONObject

/**
 * The app's language: the one its page shows (web/index.html picks it, MeshNative.language passes it here),
 * otherwise the phone's. Texts are the page's dictionaries (i18n/web/<lang>.json, in the assets as
 * web/i18n/<lang>.js) with the Russian text as the key; {0}, {1}… stand for the values. Russian needs none.
 */
object I18n {
    val LANGS = listOf("en", "ru", "uk", "es", "pt", "fr", "de", "it", "pl", "tr", "zh", "ja", "ko", "ar", "id")
    @Volatile var lang = "ru"; private set
    @Volatile private var dict: JSONObject? = null

    fun init(context: Context) {
        if (dict != null || lang != "ru") return
        val phone = context.resources.configuration.locales[0].language.let { if (it == "be") "ru" else it }
        use(context, Prefs(context).lang ?: phone.takeIf { it in LANGS } ?: "en")
    }

    fun use(context: Context, code: String) {
        val next = code.takeIf { it in LANGS } ?: "en"
        dict = if (next == "ru") null else runCatching {
            val js = context.assets.open("web/i18n/$next.js").bufferedReader().readText()
            JSONObject(js.substring(js.indexOf('{'), js.lastIndexOf('}') + 1))
        }.getOrNull()
        lang = next
        Prefs(context).lang = next
    }

    fun t(key: String, vararg values: Any?): String {
        val text = dict?.optString(key)?.takeIf { it.isNotEmpty() } ?: key
        return if (values.isEmpty()) text else PLACEHOLDER.replace(text) { m -> values.getOrNull(m.groupValues[1].toInt())?.toString() ?: m.value }
    }

    /** Piece letters K Q R B N of the move notation. */
    fun pieces(): List<String> = dict?.optString("_pieces")?.takeIf { it.isNotEmpty() }?.split(' ') ?: listOf("Кр", "Ф", "Л", "С", "К")

    private val PLACEHOLDER = Regex("\\{(\\d+)\\}")
}
