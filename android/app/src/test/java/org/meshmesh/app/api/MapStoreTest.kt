package org.meshmesh.app.api

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

class MapStoreTest {
    @get:Rule val tmp = TemporaryFolder()

    /** A one-colour tile: runs of 65535 and 1 pixels. */
    private fun tile(color: Int): ByteArray = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
        .putShort(65535.toShort()).putShort(color.toShort()).putShort(1).putShort(color.toShort()).array()

    private fun manifest(name: String) = JSONObject().put("version", 1).put("name", name).put("latitude", 46.02).put("longitude", 7.75)
        .put("bounds", org.json.JSONArray(listOf(46.0, 7.7, 46.04, 7.8))).put("zooms", org.json.JSONArray(listOf(13, 14))).put("default_zoom", 14).toString()

    private fun store(): MapStore { val phone = tmp.newFolder("phone"); val card = tmp.newFolder("card"); return MapStore { mapOf("phone" to phone, "card" to card) } }

    @Test fun savesAndReadsTilesAsBoardFiles() {
        val s = store()
        val (x, y) = MapStore.tileOf(46.02, 7.75, 14)
        val id = s.begin("phone", manifest("Valley"))
        s.tile(id, 14, x, y, tile(0x1234))
        assertNull("an unfinished area is not shown", s.read(14, x, y))
        val info = s.finish(id)
        assertEquals(1, info.getInt("tiles"))
        val f = s.read(14, x, y)!!
        val h = ByteBuffer.wrap(f).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("MMT1", String(f, 0, 4))
        assertEquals(14, f[4].toInt())
        assertEquals(x.toInt(), h.getInt(8)); assertEquals(y.toInt(), h.getInt(12)); assertEquals(8, h.getInt(16))
        assertEquals(CRC32().apply { update(tile(0x1234)) }.value.toInt(), h.getInt(20))
        assertArrayEquals(tile(0x1234), f.copyOfRange(24, f.size))
        val a = s.areas().getJSONObject(0)
        assertEquals("Valley", a.getString("name")); assertEquals("phone", a.getString("storage")); assertTrue(a.getBoolean("complete"))
        assertNull("outside the area's zooms", s.read(15, x * 2, y * 2))
    }

    @Test fun newerAreaCoversOlderAndDeleteRestores() {
        val s = store()
        val (x, y) = MapStore.tileOf(46.02, 7.75, 13)
        val old = s.begin("phone", manifest("Old")); s.tile(old, 13, x, y, tile(1)); s.finish(old)
        Thread.sleep(2)
        val new = s.begin("card", manifest("New")); s.tile(new, 13, x, y, tile(2)); s.finish(new)
        assertArrayEquals(tile(2), s.read(13, x, y)!!.copyOfRange(24, 32))
        assertArrayEquals("one area asked for", tile(1), s.read(13, x, y, old)!!.copyOfRange(24, 32))
        assertEquals(listOf("New", "Old"), (0 until 2).map { s.areas().getJSONObject(it).getString("name") })
        s.delete(new)
        assertArrayEquals(tile(1), s.read(13, x, y)!!.copyOfRange(24, 32))
        assertEquals(1, s.tiles(old).length())
    }

    @Test fun rejectsBadTilesAndManifests() {
        val s = store()
        val id = s.begin("phone", manifest("A"))
        assertTrue(runCatching { s.tile(id, 14, 1, 1, ByteArray(8)) }.isFailure) // zero runs
        assertTrue(runCatching { s.tile(id, 14, 1, 1, tile(1).copyOf(4)) }.isFailure) // 65535 pixels only
        assertTrue(runCatching { s.tile(id, 14, 1L shl 14, 1, tile(1)) }.isFailure)
        assertTrue(runCatching { s.finish(id) }.isFailure) // no tiles
        assertTrue(runCatching { s.begin("phone", JSONObject().put("name", "").toString()) }.isFailure)
        assertTrue(runCatching { s.begin("nowhere", manifest("B")) }.isFailure)
        assertFalse(MapStore.validRle(ByteArray(0)))
        assertTrue(MapStore.validRle(tile(7)))
    }
}
