package com.dashcue.feature.navigation

import java.text.Normalizer
import java.util.regex.Pattern

/**
 * 6-stage navigation text sanitization and transliteration pipeline (R6).
 *
 * Ensures all navigation cues, instructions, and street names sent to the HUD
 * contain zero unrenderable Unicode boxes [?], HTML tags, mojibake, or entity artifacts.
 * The CYD display firmware uses Montserrat ASCII/Latin-1 fonts; enforcing ASCII [32..126]
 * guarantees 100% clean rendering across all screens.
 */
object NavTextSanitizer {

    private val HTML_TAG_REGEX = Regex("</?[a-zA-Z][^>]*>|<!--[\\s\\S]*?-->")
    private val MULTI_SPACE_REGEX = Regex("\\s+")
    private val DIACRITICS_PATTERN = Pattern.compile("\\p{InCombiningDiacriticalMarks}+")
    private val NUMERIC_DEC_ENTITY = Regex("&#(\\d+);")
    private val NUMERIC_HEX_ENTITY = Regex("&#x([0-9a-fA-F]+);")

    private val RIGHT_ARROWS_REGEX = Regex("[\u2794\u279C\u27A1\u2192\u2197\u2198\u2B08\u2B0A\u21B1]")
    private val LEFT_ARROWS_REGEX = Regex("[\u2B05\u2190\u2196\u2199\u2B09\u2B0B\u21B0]")
    private val UP_ARROWS_REGEX = Regex("[\u2B06\u2191]")
    private val DOWN_ARROWS_REGEX = Regex("[\u2B07\u2193]")
    private val BULLETS_REGEX = Regex("[\u2022\u00B7\u25CF\u25CB\u25AA\u25AB|]")
    private val DOUBLE_QUOTES_REGEX = Regex("[\u201C\u201D\u201E\u201F\u00AB\u00BB]")
    private val SINGLE_QUOTES_REGEX = Regex("[\u2018\u2019\u201A\u201B\u2039\u203A]")
    private val DASHES_REGEX = Regex("[\u2013\u2014\u2015\u2212]")
    private val UNICODE_SPACES_REGEX = Regex("[\u00A0\u2000-\u200B\u202F\uFEFF\u3000]")

    /**
     * Executes the 6-stage sanitization pipeline on the input string.
     *
     * Stage 1: HTML entity decoding (named, decimal, hex)
     * Stage 2: HTML tag stripping (<...>)
     * Stage 3: Unicode arrow and navigation symbol transliteration
     * Stage 4: Diacritics normalization (NFD decomposition + Latin transliteration)
     * Stage 5: ASCII 32..126 enforcement (strip/replace unrenderable glyphs)
     * Stage 6: Whitespace collapse and trim
     */
    fun sanitize(input: String?): String {
        if (input.isNullOrBlank()) return ""

        // Stage 1: Decode HTML entities
        var s = input
            .replace("&amp;", "&")
            .replace("&quot;", "\"")
            .replace("&apos;", "'")
            .replace("&#39;", "'")
            .replace("&lt;", "<")
            .replace("&gt;", ">")
            .replace("&nbsp;", " ")
            .replace("&bull;", "-")
            .replace("&ndash;", "-")
            .replace("&mdash;", "-")
            .replace("&hellip;", "...")
            .replace("&rarr;", "->")
            .replace("&larr;", "<-")
            .replace("&uarr;", "^")
            .replace("&darr;", "v")

        s = NUMERIC_DEC_ENTITY.replace(s) { match ->
            match.groupValues[1].toIntOrNull()?.toChar()?.toString() ?: ""
        }
        s = NUMERIC_HEX_ENTITY.replace(s) { match ->
            match.groupValues[1].toIntOrNull(16)?.toChar()?.toString() ?: ""
        }

        // Stage 2: Strip HTML tags
        s = HTML_TAG_REGEX.replace(s, "")

        // Stage 3: Transliterate Unicode navigation symbols & arrows
        s = s
            .replace(RIGHT_ARROWS_REGEX, "->")
            .replace(LEFT_ARROWS_REGEX, "<-")
            .replace(UP_ARROWS_REGEX, "^")
            .replace(DOWN_ARROWS_REGEX, "v")
            .replace(BULLETS_REGEX, "-")
            .replace(DOUBLE_QUOTES_REGEX, "\"")
            .replace(SINGLE_QUOTES_REGEX, "'")
            .replace(DASHES_REGEX, "-")
            .replace("\u2026", "...")
            .replace(UNICODE_SPACES_REGEX, " ")

        // Stage 4: Diacritics & Latin ligature normalization
        s = s
            .replace("ß", "ss")
            .replace("æ", "ae")
            .replace("Æ", "AE")
            .replace("œ", "oe")
            .replace("Œ", "OE")
            .replace("ø", "o")
            .replace("Ø", "O")
            .replace("đ", "d")
            .replace("Đ", "D")
            .replace("ł", "l")
            .replace("Ł", "L")

        val normalized = Normalizer.normalize(s, Normalizer.Form.NFD)
        s = DIACRITICS_PATTERN.matcher(normalized).replaceAll("")

        // Stage 5: Enforce ASCII [32..126]
        val sb = StringBuilder(s.length)
        for (ch in s) {
            if (ch.code in 32..126) {
                sb.append(ch)
            } else {
                sb.append(' ')
            }
        }

        // Stage 6: Whitespace collapse and trim
        return MULTI_SPACE_REGEX.replace(sb.toString(), " ").trim()
    }

    /**
     * Sanitizes and bounds the string to [maxLength] characters.
     */
    fun sanitize(input: String?, maxLength: Int): String {
        return sanitize(input).take(maxLength)
    }
}
