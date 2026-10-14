package org.meshmesh.app.api

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

/**
 * Offline map areas kept in the phone (web/index.html «Карты»): the same MMT1 tiles a board keeps on its SD
 * (docs/maps-format.md), one folder per area in the app's own storage — internal memory ("phone") or the
 * phone's removable SD card ("card"). Android removes both with the app. Tiles of a newer area cover older ones.
 *
 * Folder: `<root>/<id>/area.json` (the package manifest with id, storage, tiles, bytes, saved, complete) and
 * `<root>/<id>/<z>/<x>_<y>.mmt`. An area becomes visible to [read] only when [finish] marks it complete.
 */
class MapStore(private val roots: () -> Map<String, File>) {
    private class Area(val dir: File, val info: JSONObject) {
        val id: String get() = info.getString("id")
        val saved: Long get() = info.optLong("saved")
        val complete: Boolean get() = info.optBoolean("complete")
        /** Tile range of the area's bounds at a zoom; null: no bounds, any tile may be there. */
        fun covers(z: Int, x: Long, y: Long): Boolean {
            val zooms = info.optJSONArray("zooms")
            if (zooms != null && (0 until zooms.length()).none { zooms.optInt(it) == z }) return false
            val b = info.optJSONArray("bounds") ?: return true
            if (b.length() != 4) return true
            val (x0, y1) = tileOf(b.optDouble(0), b.optDouble(1), z)
            val (x1, y0) = tileOf(b.optDouble(2), b.optDouble(3), z)
            return x in x0..x1 && y in y0..y1
        }
    }

    private var cache: List<Area>? = null

    @Synchronized private fun all(): List<Area> = cache ?: roots().flatMap { (storage, root) ->
        root.listFiles { f -> f.isDirectory }.orEmpty().mapNotNull { dir ->
            runCatching { Area(dir, JSONObject(File(dir, "area.json").readText()).put("storage", storage)) }.getOrNull()
        }
    }.sortedByDescending { it.saved }.also { cache = it }

    @Synchronized private fun forget() { cache = null }

    private fun area(id: String) = all().firstOrNull { it.id == id } ?: throw IllegalArgumentException("no such area")

    /** Storages for new areas with their free and total bytes. */
    fun storages(): JSONArray = JSONArray(roots().map { (id, dir) ->
        dir.mkdirs()
        JSONObject().put("id", id).put("free", dir.usableSpace).put("total", dir.totalSpace)
    })

    /** Saved areas, newest first. */
    fun areas(): JSONArray = JSONArray(all().map { JSONObject(it.info.toString()) })

    /** Starts an area from a package manifest; returns its id. */
    @Synchronized fun begin(storage: String, manifest: String): String {
        val root = roots()[storage] ?: throw IllegalArgumentException("storage unavailable")
        val m = JSONObject(manifest)
        val name = m.optString("name")
        require(name.isNotEmpty() && name.toByteArray().size <= 96) { "invalid area name" }
        val lat = m.optDouble("latitude"); val lon = m.optDouble("longitude")
        require(lat.isFinite() && kotlin.math.abs(lat) <= 85.05112878 && lon.isFinite() && kotlin.math.abs(lon) <= 180) { "invalid area position" }
        var id = "a${System.currentTimeMillis()}"
        while (File(root, id).exists()) id += "x"
        val dir = File(root, id)
        check(dir.mkdirs()) { "cannot create the area folder" }
        val info = m.put("id", id).put("saved", System.currentTimeMillis()).put("complete", false).put("tiles", 0).put("bytes", 0)
        File(dir, "area.json").writeText(info.toString())
        forget()
        return id
    }

    /** One tile of an area being saved: the RLE payload of a package record (docs/maps-format.md). */
    fun tile(id: String, z: Int, x: Long, y: Long, payload: ByteArray) {
        require(z in 0..18 && x >= 0 && y >= 0 && x < (1L shl z) && y < (1L shl z)) { "invalid tile coordinates" }
        require(validRle(payload)) { "invalid tile data" }
        val a = area(id)
        check(!a.complete) { "area already saved" }
        val crc = CRC32().apply { update(payload) }.value
        val head = ByteBuffer.allocate(HEADER).order(ByteOrder.LITTLE_ENDIAN)
        head.put("MMT1".toByteArray()).put(z.toByte()).put(0).put(0).put(0)
            .putInt(x.toInt()).putInt(y.toInt()).putInt(payload.size).putInt(crc.toInt())
        val target = File(a.dir, "$z/${x}_$y.mmt")
        target.parentFile!!.mkdirs()
        val part = File(target.path + ".part")
        part.outputStream().use { it.write(head.array()); it.write(payload) }
        if (target.exists()) target.delete()
        check(part.renameTo(target)) { "cannot save the tile" }
    }

    /** Marks an area complete; returns the area. */
    @Synchronized fun finish(id: String): JSONObject {
        val a = area(id)
        val files = tileFiles(a.dir)
        check(files.isNotEmpty()) { "no tiles" }
        a.info.put("complete", true).put("tiles", files.size).put("bytes", files.sumOf { it.length() })
        val out = JSONObject(a.info.toString()).apply { remove("storage") }
        val f = File(a.dir, "area.json"); val part = File(a.dir, "area.json.part")
        part.writeText(out.toString())
        f.delete(); check(part.renameTo(f)) { "cannot save the area" }
        forget()
        return a.info
    }

    @Synchronized fun delete(id: String) {
        all().firstOrNull { it.id == id }?.dir?.deleteRecursively()
        forget()
    }

    /** Tiles of an area as [z,x,y] (to copy it to a board's SD). */
    fun tiles(id: String): JSONArray = JSONArray(tileFiles(area(id).dir).mapNotNull { f ->
        val z = f.parentFile!!.name.toIntOrNull(); val xy = f.nameWithoutExtension.split('_')
        if (z == null || xy.size != 2) null else JSONArray().put(z).put(xy[0].toLong()).put(xy[1].toLong())
    }.sortedWith(compareBy({ it.getInt(0) }, { it.getLong(2) }, { it.getLong(1) })))

    /** The MMT1 file of a tile from the newest complete area that has it, or from one area. */
    fun read(z: Int, x: Long, y: Long, area: String? = null): ByteArray? {
        for (a in all()) {
            if (!a.complete || (area != null && a.id != area) || (area == null && !a.covers(z, x, y))) continue
            val f = File(a.dir, "$z/${x}_$y.mmt")
            if (f.isFile) return runCatching { f.readBytes() }.getOrNull() ?: continue
        }
        return null
    }

    private fun tileFiles(dir: File) = dir.listFiles { f -> f.isDirectory }.orEmpty()
        .flatMap { it.listFiles { f -> f.name.endsWith(".mmt") }.orEmpty().toList() }

    companion object {
        const val HEADER = 24

        /** uint16 LE run length, uint16 LE RGB565 pairs; nonzero runs totalling exactly 256×256 pixels. */
        fun validRle(a: ByteArray): Boolean {
            if (a.isEmpty() || a.size % 4 != 0 || a.size > 262144) return false
            var total = 0
            for (i in a.indices step 4) {
                val n = (a[i].toInt() and 255) or ((a[i + 1].toInt() and 255) shl 8)
                if (n == 0) return false
                total += n
                if (total > 65536) return false
            }
            return total == 65536
        }

        /** Web Mercator tile of a point. */
        fun tileOf(lat: Double, lon: Double, z: Int): Pair<Long, Long> {
            val n = (1L shl z).toDouble()
            val r = Math.toRadians(lat.coerceIn(-85.05112878, 85.05112878))
            val x = ((lon + 180) / 360 * n).toLong().coerceIn(0, (1L shl z) - 1)
            val y = ((1 - kotlin.math.ln(kotlin.math.tan(r) + 1 / kotlin.math.cos(r)) / Math.PI) / 2 * n).toLong().coerceIn(0, (1L shl z) - 1)
            return x to y
        }
    }
}
