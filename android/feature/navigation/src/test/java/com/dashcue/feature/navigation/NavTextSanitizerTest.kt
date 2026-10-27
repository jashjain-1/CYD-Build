package com.dashcue.feature.navigation

import org.junit.Assert.*
import org.junit.Test

class NavTextSanitizerTest {

    @Test
    fun testNullAndBlankInput() {
        assertEquals("", NavTextSanitizer.sanitize(null))
        assertEquals("", NavTextSanitizer.sanitize(""))
        assertEquals("", NavTextSanitizer.sanitize("   \t\n  "))
    }

    @Test
    fun testHtmlEntitiesDecoding() {
        val input = "Salt &amp; Pepper &quot;Main&quot; &#39;St&#39; &lt;1&gt; &nbsp;&hellip;"
        val result = NavTextSanitizer.sanitize(input)
        assertEquals("Salt & Pepper \"Main\" 'St' <1> ...", result)
    }

    @Test
    fun testNumericEntitiesDecoding() {
        val input = "Route &#65; and &#x42;"
        val result = NavTextSanitizer.sanitize(input)
        assertEquals("Route A and B", result)
    }

    @Test
    fun testHtmlTagStripping() {
        val input = "<div class=\"nav-cue\"><b>Turn right</b> on <span style=\"color:red\">Highway 101</span></div>"
        val result = NavTextSanitizer.sanitize(input)
        assertEquals("Turn right on Highway 101", result)
    }

    @Test
    fun testArrowTransliteration() {
        assertEquals("-> Market St", NavTextSanitizer.sanitize("➔ Market St"))
        assertEquals("-> 5th Ave", NavTextSanitizer.sanitize("➡ 5th Ave"))
        assertEquals("<- Exit 4B", NavTextSanitizer.sanitize("⬅ Exit 4B"))
        assertEquals("^ Continue straight", NavTextSanitizer.sanitize("⬆ Continue straight"))
        assertEquals("v Keep underpass", NavTextSanitizer.sanitize("⬇ Keep underpass"))
        assertEquals("-> Right ramp", NavTextSanitizer.sanitize("↱ Right ramp"))
        assertEquals("<- Left ramp", NavTextSanitizer.sanitize("↰ Left ramp"))
    }

    @Test
    fun testPunctuationAndSymbols() {
        val input = "• Start · Next ● End – Middle — Long “Quoted” ‘Single’ \u00A0 spaced"
        val result = NavTextSanitizer.sanitize(input)
        assertEquals("- Start - Next - End - Middle - Long \"Quoted\" 'Single' spaced", result)
    }

    @Test
    fun testDiacriticsAndLigatures() {
        assertEquals("Champs-Elysees", NavTextSanitizer.sanitize("Champs-Élysées"))
        assertEquals("Munchen Strasse", NavTextSanitizer.sanitize("München Straße"))
        assertEquals("Kobenhavn", NavTextSanitizer.sanitize("København"))
        assertEquals("Curacao", NavTextSanitizer.sanitize("Curaçao"))
        assertEquals("Sao Paulo", NavTextSanitizer.sanitize("São Paulo"))
    }

    @Test
    fun testAsciiEnforcementAndEmojiRemoval() {
        val input = "Drive toward Tokyo 東京 🚗💨"
        val result = NavTextSanitizer.sanitize(input)
        // Non-ASCII CJK and emoji should be replaced with spaces and collapsed
        assertEquals("Drive toward Tokyo", result)
        for (ch in result) {
            assertTrue("Code point ${ch.code} must be in 32..126", ch.code in 32..126)
        }
    }

    @Test
    fun testWhitespaceCleanup() {
        val input = "  Turn   \t\n  right   onto   Main   St  "
        val result = NavTextSanitizer.sanitize(input)
        assertEquals("Turn right onto Main St", result)
    }

    @Test
    fun testMaxLengthBounding() {
        val input = "This is a very long navigation instruction that exceeds the standard forty-eight character street limit"
        val result = NavTextSanitizer.sanitize(input, 48)
        assertEquals(48, result.length)
        assertTrue(input.startsWith(result))
    }

    @Test
    fun testAdversarialNestedHtmlAndMalformedEntities() {
        val adversarialInputs = listOf(
            "<div><span><p><a href=\"http://evil.com\">Turn <b>Right</b></a></p></span></div>",
            "<<<div<script>alert(1)</script>>Turn left>>>",
            "&amp;&amp;&quot;&lt;&gt;&#39;&#x41;&#66;&bull;&mdash;&rarr;&larr;",
            "&#99999999999999999999999; and &#xZZZZZZZZ; and &#-1; and &#x-1;",
            "&; &&; &#; &#x; &amp &quot &lt &gt",
            "<!-- unclosed comment <!-- nested --> Continue on <b>I-95</b> -->",
            "<svg/onload=alert('XSS')> Turn <img src=x onerror=alert(1)> North",
            "<a\nb\nc\nd>Keep right</a\n>",
        )

        for (input in adversarialInputs) {
            val result = NavTextSanitizer.sanitize(input)
            for (ch in result) {
                assertTrue("Code point ${ch.code} ('$ch') in result '$result' must be in 32..126", ch.code in 32..126)
            }
        }
    }

    @Test
    fun testAdversarialExoticUnicodeAndDiacritics() {
        // Zalgo text (stacked combining diacritics)
        val zalgo = "T̷̫̎u̶̬͑r̵̫̔n̸̜͌ ̷̢̿R̴͕͊i̸̔͜g̶̱͠h̸͉͝ț̵̐"
        val zalgoResult = NavTextSanitizer.sanitize(zalgo)
        for (ch in zalgoResult) {
            assertTrue("Zalgo char ${ch.code} must be in 32..126", ch.code in 32..126)
        }
        assertEquals("Turn Right", zalgoResult)

        // Math Fraktur, Braille, Musical, Egyptian Hieroglyphs
        val exotic = "𝕿𝖚𝖗𝖓 𝖑𝖊𝖋𝖙 ⠞⠥⠗⠝ 𝄞𝄢 𓀀𓀁 onto 𝕸𝖆𝖎𝖓 𝕾𝖙"
        val exoticResult = NavTextSanitizer.sanitize(exotic)
        for (ch in exoticResult) {
            assertTrue("Exotic char ${ch.code} must be in 32..126", ch.code in 32..126)
        }

        // Combining marks without base characters
        val orphanMarks = "\u0300\u0301\u0302\u0303\u0304 Exit 12 \u0305\u0306"
        val orphanResult = NavTextSanitizer.sanitize(orphanMarks)
        assertEquals("Exit 12", orphanResult)
    }

    @Test
    fun testAdversarialRtlAndBidirectionalText() {
        val rtlInputs = listOf(
            "انعطف يميناً إلى شارع الرئيسي", // Arabic
            "פנה ימינה לרחוב הראשי", // Hebrew
            "\u202E\u200E\u200F\u202A\u202B\u202C\u202D Reversed Nav Cue \u202C", // Bidirectional override controls
            "Turn right -> شارع 101 -> Exit 4"
        )
        for (input in rtlInputs) {
            val result = NavTextSanitizer.sanitize(input)
            for (ch in result) {
                assertTrue("Code point ${ch.code} in result '$result' must be in 32..126", ch.code in 32..126)
            }
        }
    }

    @Test
    fun testAdversarialMassivePayloadsAndBoundaryLengths() {
        val massiveHtml = "<div>".repeat(500) + "Highway 101 North" + "</div>".repeat(500)
        val massiveResult = NavTextSanitizer.sanitize(massiveHtml, 48)
        assertEquals(17, massiveResult.length)
        assertEquals("Highway 101 North", massiveResult)

        val longGibberish = (0..10000).map { (it % 1000).toChar() }.joinToString("")
        val sanitized = NavTextSanitizer.sanitize(longGibberish, 96)
        assertTrue(sanitized.length <= 96)
        for (ch in sanitized) {
            assertTrue("Code point ${ch.code} must be in 32..126", ch.code in 32..126)
        }
    }

    @Test
    fun testFuzzRandomUnicodeAlwaysProducesCleanAscii() {
        val random = java.util.Random(0xC001CAFE)
        for (i in 0..500) {
            val len = random.nextInt(200) + 1
            val chars = CharArray(len) {
                random.nextInt(0x10000).toChar()
            }
            val input = String(chars)
            val result = NavTextSanitizer.sanitize(input)
            for (ch in result) {
                assertTrue("Fuzz iteration $i: char ${ch.code} must be in 32..126", ch.code in 32..126)
            }
        }
    }
}
