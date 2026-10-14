package org.meshmesh.app

import org.json.JSONArray
import org.json.JSONObject

/**
 * Chess news for notifications: the board's game list (/api/chess "games") compared with the previous
 * one, as the web page does, so the text does not depend on the board's menu language. One note per game.
 */
object ChessNews {
    data class Note(val game: String, val title: String, val text: String)

    fun between(before: JSONArray, after: JSONArray): List<Note> {
        val old = HashMap<String, JSONObject>()
        for (i in 0 until before.length()) before.getJSONObject(i).let { old[it.optString("id")] = it }
        val notes = ArrayList<Note>()
        for (i in 0 until after.length()) {
            val g = after.getJSONObject(i)
            val id = g.optString("id")
            val name = g.optString("name").ifBlank { I18n.t("Соперник") }
            val o = old[id]
            val text = when {
                o == null -> if (g.optString("state") == "invited") state(g) else null
                g.optInt("plies") > o.optInt("plies") && lastByThem(g) ->
                    ruSan(g.optString("last_san")) + " — " + state(g).lowercase()
                o.optString("state") != "over" && g.optString("state") == "over" -> state(g)
                o.optString("state") == "inviting" && g.optString("state") == "playing" -> I18n.t("Вызов принят") + if (g.optBoolean("my_turn")) I18n.t(" — ваш ход") else ""
                o.optString("draw_offer") != "theirs" && g.optString("draw_offer") == "theirs" && g.optString("state") == "playing" -> I18n.t("Предлагает ничью")
                else -> null
            } ?: continue
            notes += Note(id, name, text)
        }
        return notes
    }

    /** K Q R B N in the notation of the app's language (Russian Кр Ф Л С К), as on the page. */
    fun ruSan(san: String): String {
        val pieces = I18n.pieces()
        val names = "KQRBN".mapIndexed { i, c -> c to pieces[i] }.toMap()
        val out = StringBuilder()
        san.forEachIndexed { i, c -> out.append(if ((i == 0 || san[i - 1] == '=') && c in names) names[c] else c) }
        return out.toString()
    }

    private fun lastByThem(g: JSONObject): Boolean {
        val plies = g.optInt("plies")
        return plies > 0 && (if (plies % 2 == 1) "white" else "black") != g.optString("color")
    }

    fun state(g: JSONObject): String {
        when (g.optString("state")) {
            "inviting" -> return I18n.t("Вызов отправлен, ждём ответа")
            "invited" -> return I18n.t("Вызывает вас: вы играете ") + if (g.optString("color") == "white") I18n.t("белыми") else I18n.t("чёрными")
            "playing" -> return if (g.optString("draw_offer") == "theirs") I18n.t("Предлагает ничью")
                else if (g.optBoolean("my_turn")) (if (g.optBoolean("check")) I18n.t("Ваш ход: шах!") else I18n.t("Ваш ход")) else I18n.t("Ход соперника")
        }
        val reason = g.optString("reason")
        if (reason == "declined") return I18n.t("Вызов отклонён")
        if (reason == "cancelled") return I18n.t("Партия отменена")
        val result = g.optString("result")
        val won = result.isNotEmpty() && result == g.optString("color")
        val lost = result.isNotEmpty() && result != "draw" && !won
        val why = mapOf("mate" to I18n.t(": мат"), "resigned" to if (won) I18n.t(": соперник сдался") else I18n.t(": вы сдались"), "stalemate" to I18n.t(": пат"),
            "repetition" to I18n.t(": троекратное повторение"), "fifty" to I18n.t(": 50 ходов без взятий"), "material" to I18n.t(": мало фигур для мата"),
            "too_long" to I18n.t(": предел ходов"), "agreed" to I18n.t(" по соглашению"))[reason] ?: ""
        return (if (won) I18n.t("Победа") else if (lost) I18n.t("Поражение") else I18n.t("Ничья")) + why
    }
}
