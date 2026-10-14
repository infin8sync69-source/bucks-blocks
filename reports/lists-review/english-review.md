# English list review (en-draft-2048)

Scope: all 2048 rows of `wordlists/review/ar-quran-draft-review.csv` (index, lemma, pos, root, English gloss). Read-only review; nothing in `wordlists/`, `tools/` or `src/` was changed. Companion file: `wordlists/review/english-proposed-changes.csv` (one row per flagged index, with a proposed replacement where one exists).

## Summary

| Severity | Rows | With a proposed replacement | Meaning |
|---|---|---|---|
| Must-fix | 22 | 22 | Certain defects: homophones, singular/plural of one word, non-words, a slur look-alike |
| Should-fix | 230 | 222 | Strong case: word-family pairs, wrong or archaic glosses, hidden Arabic duplicates, insulting words |
| Consider | 234 | 47 | Judgement calls: vague glosses, forced synonyms, mildly sensitive words, spelling variants |
| Total flagged | 486 of 2048 (24%) | 291 | |

Counts by issue type and severity:

| Issue type | Must-fix | Should-fix | Consider | Total |
|---|---|---|---|---|
| inflection-pair | 0 | 170 | 13 | 183 |
| vague | 0 | 1 | 61 | 62 |
| archaic | 0 | 18 | 22 | 40 |
| sensitive | 1 | 6 | 32 | 39 |
| arabic-duplicate | 0 | 10 | 27 | 37 |
| wrong-gloss | 0 | 17 | 20 | 37 |
| forced-synonym | 0 | 0 | 35 | 35 |
| odd-word | 2 | 4 | 10 | 16 |
| confusable | 0 | 3 | 12 | 15 |
| plural-pair | 13 | 1 | 0 | 14 |
| homophone | 6 | 0 | 0 | 6 |
| long-word | 0 | 0 | 2 | 2 |

Headline findings:

- The glosses are mostly sound as dictionary-form meanings. Of the 2048 rows, roughly 1560 need no action. The defects are concentrated in a few repeatable patterns (below), not scattered errors.
- The biggest single pattern is **one English word family spread over several Arabic entries** (follow/follower, obey/obeyed/obedience/obedient, intercede/intercessor/intercession). It accounts for most Should-fix rows. It is harmless for meaning (the Arabic entries differ) but undermines the typing-safety goal of T2/T4 and makes phrases look repetitive.
- 13 exact singular/plural pairs (camel/camels, fruit/fruits, guard/guards, youth/youths, ...) and 6 homophone pairs (see/sea, son/sun, bare/bear, loan/lone, wade/weighed, lightening/lightning) are certain defects and are cheap to fix.
- `niggardly` (1172) should go: it reads as a slur to many readers whatever its etymology. `wetnurse` (1412) and `stiffnecked` (1561) are not standard one-word English forms.
- 37 rows belong to pairs or clusters of Arabic entries with identical true meaning (gender variants such as عالي/عالية, same-root forms such as جهار/جهرة/جهر/علانية = "openly"). English cannot separate them honestly; the real fix is on the Arabic side (swap in a different lemma) or to accept the pair.

## Method

1. Read every row (index, lemma with vowels, pos, root, gloss) against my knowledge of Quranic Arabic and the English words. I did not have the Quranic Arabic Corpus gloss column, so the meaning checks rest on the lemma, pos and root, not on a third-party gloss.
2. Mechanical passes over the 2048 glosses (Python, isolated mode): all distinct and `[a-z]+`; every pair at edit distance 1 (373 pairs); same-stem and word-family pairs (shared prefix of at least 4-5 letters plus short suffix); exact singular/plural pairs; a hand-built table of common homophone groups intersected with the list; words absent from `/usr/share/dict/words` after stripping common endings; length and first-four-letter statistics; US/UK spelling pairs.
3. Manual semantic pass for items no script can find: wrong gloss, wrong part of speech, forced synonyms, hidden Arabic duplicates, archaic or loaded words.
4. Replacements were chosen by hand, then verified programmatically: every proposed word is a single lowercase `a-z` word, absent from the current column, unique among proposals, and (checked against the column as it would look after all changes) creates no edit-distance-1 pair with a word of up to 7 letters (one known exception: `bleed`/`blend`, noted), no homophone pair, no duplicate and no exact plural pair. 291 proposals, 291 distinct.
5. Severity: Must-fix = certain defect or hard-rule problem (T2/T4 spirit); Should-fix = I would change it; Consider = defensible either way, flagged so the owner can decide.

Verification command (isolated mode) used on the CSV: it reads the review CSV and the proposals CSV and asserts regex `^[a-z]+$`, not in the current column, no duplicate proposals, and `current_english` equals the review CSV value for every row. All assertions held.

## Systematic patterns

**1. Word families reused across Arabic entries (about 170 rows flagged Should-fix, plus 13 exact plural pairs).** Examples: follow/follower, deserve/deserving, seek/seeking, eat/eating, obey/obeyed/obedient/obedience, intercede/intercessor/intercession, loan/loaned, lose/loss/loser, dwell/dwellers/dwelling, attend/attending/attended/attendants. Cause: the Arabic list holds several morphological forms of one root (lemma-level duplication in the Arabic side), and the English column mirrors them. Fix: change the derived member to a different word; about 170 replacements proposed for this pattern.

**2. Hidden Arabic duplicates (37 rows flagged, no English fix).** Gender and number variants of one lemma are separate Arabic entries (عالي/عالي/عالية, ذلل/ذلول, بازغ/بازغة, غمرة/غمرة, محصنة/محصَّنة). The English glosses (high/elevated/superior/uppermost) hide that the Arabic means the same thing, which breaks T1/T6 in spirit. Same-root clusters behave the same way: "openly" has four Arabic entries (جهار, جهرة, جهر, علانية), "livelihood" four (معاش, معايش, معيشة, عيشة), "fabricate/slander" seven (the فري family), "marry/wed" three. Recommendation: replace the redundant Arabic lemma, not the English word.

**3. Short-word near-collisions (T4).** 373 pairs of glosses are one letter apart; 260 of them involve only words of 5 letters or fewer, 166 only words of 4 letters or fewer (a minimum change set for those 166 pairs would touch about 82 words). Examples: ear/bear/fear/far/err/eat, sin/son/sun/sit/win/skin, eight/light/might/sight/fight/tight, war/wary/warm/ward. Most cannot be fixed without losing the only good word for the meaning (ear, sun, sin, war), so I did not flag them one by one: the applied proposals reduce the 373 pairs to 288 as a side effect, and the full pair list is in the appendix. Decision for the owner: either relax T4 to words of 3-4 letters, or accept it as published statistics as the research note itself suggests. The ones that matter most (homophones and same-family pairs) are flagged.

**4. Participle and noun-for-verb glosses.** Of 620 verb lemmas, 61 are glossed with an adjective or participle (arrogant, independent, tranquil, fearful). Many of the 1100 participle-style noun lemmas carry -ed/-ing glosses (225 end in -ed, 168 in -ing, 34 in -ly). This is within the stated "dictionary form only" limitation, but it drives pattern 1. Consider a convention (verb lemma = bare verb) in the next revision.

**5. Sensitive and religiously loaded words.** slave, crone, fool, simpleton, deviant, niggardly, libertine, debauched, tryst, bloodshed, massacre, slaughter, assault, demon, sorcerer, crucify, swine, gambling, intoxicated, nakedness, scum, carrion. Seven read as insulting or violent inside a random phrase (rated Must/Should); the rest are Consider because they are accurate for the Arabic.

**6. Archaic or literary glosses.** twain, nigh, tidings, behold, morn, sire, hither, alas, bide, behoove, handmaid, stripling, hearkening, swoon, haled, servitor, calumny, requite, argent, forswear, adjure. All are real words, but a person copying a phrase may not know the spelling. 18 flagged Should-fix, 22 Consider.

**7. Spelling variants.** Mixed US and UK spelling: favor, honor, color, center, counselor, skeptical, savor, savior, leveled, traveler are US; ploughing and marshalled are UK. A user typing the other variant will fail the lookup. Pick US throughout (ploughing -> plowing, marshalled -> marshaled) or add a fold rule. Note: the proposed replacement for marshalled avoids the issue.

**8. Long words.** 221 glosses have 10 or more letters (e.g. inviolability, recalcitrance, differentiate). Long words are slow to type and the main source of typos. Not flagged individually except two extreme cases.

**9. First-four-letter sharing.** 998 words share their first four letters with at least one other word; 1428 distinct 4-letter prefixes for 2048 words. Extreme groups: comp (16 words), over (12), cont (11), cons (10), inte (9), dist (9), appr (8). Acceptable as a spelling-prefix input (research note accepts it), but a prefix-autocomplete feature would not work with 4 letters; 5-6 would be needed.

## Reviewer judgements worth stating

- Verb lemmas glossed with a bare verb are fine even when the surface Arabic is conjugated (known limitation by design). I did not flag those.
- Comparatives (أكثر, أفضل) are glossed with positive adjectives in places (most, greater); I flagged only where this clashes with another entry.
- I did not flag religious terms that the design deliberately neutralised (serve for worship, sky for heaven, prudent for God-fearing).
- Proposed replacements are meaning-checked by me, not by a second reviewer; please have an Arabic-English reader confirm the 22 Must-fix and the wrong-gloss replacements before applying. Applying all proposals changes 291 of 2048 entries; run `check_wordlists` after applying and expect a second small round (new near-pairs in the cascade).

## Flagged rows by issue type

Columns: index, Arabic lemma, current English, severity (M = Must-fix, S = Should-fix, C = Consider), proposed English, reason. Full data with exact severities is in the CSV.

### inflection-pair (183)

Two or more forms of one English word family (follow/follower, obey/obedience) used for different Arabic entries.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 17 | ابْتِغاء | seeking | S | search | seeking/seek (17 vs 338): same root, one gloss is an inflection of the other; easy to confuse when written |
| 25 | اتِّباع | adherence | S | allegiance | adherence (25) and adhere (1758) |
| 51 | أُحْصِرُ | restrained | S | besieged | restrained (51) and restrain (1988) |
| 55 | أَحَقّ | deserving | S | entitled | deserving/deserve (55 vs 100): same word family; 55 is comparative "more entitled" |
| 68 | أَخْطَأْ | err | S | mistake | error/err (948 vs 68): same word family; 949 ضلالة has identical meaning to 948 |
| 127 | أَشْتات | scattered | S | separately | scattered/scatter (127 vs 219): same word family |
| 140 | أَصْغَر | smaller | S | lesser | smaller (140) and small (919) |
| 141 | إِصْلاح | reform | S | improvement | reformer/reform (1477 vs 141) |
| 149 | إِطْعام | feeding | S | catering | feed/feeding (1973 vs 149) |
| 157 | إِعْراض | aversion | S | indifference | aversion (157), averse (1213), averting (1501), avert (1864): four forms |
| 174 | أَفْرِغْ | pour | S | infuse | poured/pour (1450 vs 174) |
| 180 | أُقِّتَتْ | appointed | S | scheduled | appointed (180) and appointment (1651) |
| 195 | أَكْل | eating | S | nourishment | eating/eat (195 vs 335): same word family, one letter-block apart |
| 209 | أُمْطِرَتْ | rained | S | showered | rain/rained (1485 vs 209) |
| 227 | إِنشاء | origination | S | creation | origination (227), originator (1098), originate (1909): three forms |
| 258 | باخِع | grieving | S | heartbroken | grieving (258) and grieve (334) |
| 262 | بارِز | emerging | S | prominent | emerge (290), emerging (262), emergence (1486): three forms |
| 269 | باقِيَة | remaining | S | residual | remaining (269) and remain (980) |
| 277 | تابِع | follower | S | disciple | follow (26) and follower (277) |
| 304 | طارِد | expeller | S | chaser | expeller (304) and expelled (1383), expulsion (65) |
| 314 | بَغْي | aggression | S | oppression | aggressor/aggression (1496 vs 314) |
| 342 | تَبْدِيل | alteration | S | conversion | alter/alteration (1887 vs 342) |
| 368 | يَجْهَلُ | ignorant | S | unaware | ignorant/ignorance (368 vs 278): same word family |
| 372 | تَحَصُّن | chastity | S | propriety | chastity/chaste (372 vs 52): same word family; sexual-adjacent |
| 375 | تَحِيَّة | greeting | S | salutation | greeting (375) and greet (2010) |
| 381 | تَخْسِير | loss | S | shortfall | lose/loss/loser (380, 381, 619): three forms of one word family in one list; loss (381) with lose (380) and loser (619): one word family |
| 393 | تَذْهَلُ | distracted | S | dazed | distracted (393) and distract (481) |
| 400 | تَرْتِيل | measured | S | unhurried | measured (400) and measure (465) |
| 419 | أَسْمَعَ | proclaim | S | inform | proclaim (419) and proclaimer (1594) |
| 427 | تَصَدَّى | attend | S | tackle | attend (427), attending (571), attended (1348), attendants (1510): four forms |
| 436 | أَطاعَ | obey | S | yield | obedience/obey (955 vs 436); obeyed (1972) joins obey, obedience, obedient: four forms |
| 444 | تَعْثَ | corrupt | S | ravage | corruption/corrupt (1136 vs 444) |
| 460 | تَفْضِيل | preference | S | priority | preference/prefer (460 vs 2): same word family |
| 466 | تَقَرَّ | delighted | S | elated | delight (323) and delighted (466) |
| 474 | تَكْذِيب | denial | S | refusal | denier/denial (1569 vs 474) |
| 527 | جامِع | assembler | S | compiler | assembler (527), assembly (549), assembled (1342): three forms |
| 550 | جُمْلَة | entirety | S | totality | entirety (550), entire (1222), entirely (1218): three forms |
| 560 | جَواب | response | S | answer | respond/response (1940 vs 560) |
| 566 | حاسِب | reckoner | S | accountant | reckoner (566) and reckon (1908) |
| 569 | حاشِر | summoner | S | mobilizer | summoner (569) and summon (1928) |
| 573 | حاضِرِي | dwellers | S | locals | dwellers (573) belongs to the dwell/dwelling family (121, 1858) |
| 574 | حافِظَة | guardian | S | steward | guardian (574), guard (605), guarded (1576): family of the same word |
| 577 | حامِيَة | scalding | S | seething | scalding (577) and scald (1966) |
| 581 | حِجاب | veil | S | curtain | veiled (500) and veil (581) |
| 608 | حَلال | permissible | S | lawful | permission (260) and permissible (608) |
| 612 | حَنِيذ | roasted | S | baked | roasted (612) and roast (1713) |
| 618 | خاسِرَة | ruinous | S | unprofitable | ruin (337) and ruinous (618) |
| 619 | خاسِر | loser | S | bankrupt | loser (619) with lose (380) and loss (381) |
| 625 | خافِضَة | lowering | S | demoting | lowering (625) and lower (1090) |
| 632 | خاوِيَة | collapsed | S | desolate | collapsed (632) and collapse (1763) |
| 654 | خَصِيم | disputant | S | litigant | dispute (362) and disputant (654) |
| 673 | خِيفَة | apprehension | S | misgiving | apprehend (511) and apprehension (673) |
| 695 | دِفْء | warmth | S | snugness | warm (429) and warmth (695) |
| 715 | رابِي | swelling | S | billowing | swell/swelling (1929 vs 715) |
| 717 | راجِع | returning | S | recurring | return (461) and returning (717) |
| 721 | راغِب | desirous | S | zealous | desire (130) and desirous (721) |
| 749 | رُقُود | slumbering | S | napping | slumber/slumbering (1598 vs 749) |
| 757 | رَهِين | pledged | S | pawned | pledge (276) and pledged (757) |
| 765 | زاهِق | perishing | S | dwindling | perishing (765) and perish (1878) |
| 769 | زُحْزِحَ | distanced | S | wrested | distance (268) and distanced (769) |
| 842 | سَمّاع | listener | S | audience | listen/listener (1945 vs 842) |
| 857 | هَزَمُ | rout | S | crush | routed/rout (1634 vs 857) |
| 879 | شَفاعَة | intercession | S | petition | intercessor/intercession (861 vs 879): same word family; also mediator (880) for same root; intercession (879) with intercessor (861) and intercede... |
| 883 | شُكْر | gratitude | S | indebtedness | grateful/gratitude (862 vs 883): same word family; gratitude (883) with grateful (862) and thank (1960) |
| 889 | شُورَى | consultation | S | parley | consult (414) and consultation (889) |
| 936 | ضاحِك | laughing | S | chuckling | laugh/laughing (1969 vs 936) |
| 941 | ضَبْح | panting | S | snorting | pant/panting (2013 vs 941) |
| 967 | طُغْيان | tyranny | S | despotism | tyrant/tyranny (956 vs 967): same word family, close spelling |
| 975 | طَوْع | willingly | S | voluntarily | willing/willingly (957 vs 975) |
| 977 | طَيِّبَة | agreeable | S | benign | agree (397) and agreeable (977) |
| 1040 | عُلُوّ | loftiness | S | altitude | lofty (442) and loftiness (1040) |
| 1047 | عِوَج | crookedness | S | curvature | crooked (203) and crookedness (1047) |
| 1055 | غائِبَة | concealed | S | unseen | conceal (477) and concealed (1055) |
| 1062 | غَرْبِيَّة | western | S | occidental | west/western (1520 vs 1062) |
| 1071 | غَفْلَة | negligence | S | carelessness | negligent/negligence (1052 vs 1071) |
| 1093 | يَرْهَبُ | fearful | S | intimidate | fear (57) and fearful (1093) |
| 1123 | خَلَفَ | replace | S | supplant | replacement (97) and replace (1123) |
| 1154 | قاسِيَة | hardened | S | callous | hardened/harden (1154 vs 1183) |
| 1155 | قاصِد | moderate | S | intermediate | moderate/moderation (1155 vs 1188) |
| 1161 | قاعِد | seated | S | reclining | sit/sitting/seated (470, 1194, 1161): three forms of one verb |
| 1162 | قانِتَة | obedient | S | pious | obedient/obey/obedience (1162, 436, 955): three forms of one word family |
| 1190 | قَصِيّ | remote | S | outlying | remoteness (310) and remote (1190) |
| 1199 | قَمْطَرِير | calamitous | S | harrowing | calamity (1100) and calamitous (1199) |
| 1215 | كاشِف | remover | S | dispeller | remove (79) and remover (1215) |
| 1216 | كاشِفَة | unveiler | S | uncoverer | unveiler (1216) and unveiled (1803) |
| 1222 | كامِلَيْن | entire | S | whole | entire (1222) belongs to the entirety/entirely family (550, 1218) |
| 1225 | كَبَد | toil | S | drudgery | toiling (1211) and toil (1225) |
| 1226 | كَثْرَة | abundance | S | multitude | abundant/abundance (1058 vs 1226) |
| 1235 | كَرَّمْ | dignify | S | glorify | dignity/dignify (1846 vs 1235) |
| 1241 | كَظِيم | silently | S | brooding | silent (231) and silently (1241) |
| 1257 | لاعِب | player | S | jester | player/play (1257 vs 1273) |
| 1278 | لِقاء | encounter | S | reunion | encountering/encounter (1580 vs 1278); 1579 ملاق/ملاقوا are singular/plural of one lemma |
| 1281 | مُحْى | reviving | S | animator | revive (228) and reviving (1281) |
| 1283 | لَوّاحَة | scorching | S | searing | scorch (479) and scorching (1283) |
| 1289 | لَيّ | twisting | S | wrenching | twist/twisting (2014 vs 1289) |
| 1294 | ماكِث | staying | S | sojourning | staying/stay (1294 vs 210) |
| 1317 | مُتَبَّر | destroyed | S | shattered | destroyed (1317) and destroyer (1636) |
| 1319 | مُتَّبَع | pursued | S | tracked | pursuit (969) and pursued (1319) |
| 1320 | مُتَجانِف | inclining | S | biased | incline (406) and inclining (1320) |
| 1332 | مُتَفَرِّقَة | dispersed | S | sporadic | disperse (236) and dispersed (1332) |
| 1342 | مُجْتَمِع | assembled | S | united | assembled (1342) belongs to the assembly family (527, 549) |
| 1348 | مُحْتَضَر | attended | S | convened | attended (1348) belongs to the attend family (427, 571, 1510) |
| 1378 | مُخْلِص | devoted | S | wholehearted | devotion (340) and devoted (1378) |
| 1403 | مُرْدِف | successive | S | ensuing | successively (358) and successive (1403) |
| 1413 | مَرْضِيّ | acceptable | S | satisfactory | acceptable (1413) and accept (1892) |
| 1423 | مَزِيد | additional | S | extra | addition (787) and additional (1423) |
| 1448 | مُسْفِرَة | radiant | S | beaming | radiance (951) and radiant (1448) |
| 1449 | مَسْفُوح | spilled | S | outpoured | spilled (1449) pairs with spill (proposed for 1865) |
| 1451 | مَسْكُونَة | inhabited | S | populated | inhabitant (700) and inhabited (1451) |
| 1456 | مُسْوَدّ | darkened | S | shadowed | darken (1026) and darkened (1456) |
| 1461 | مُشْتَبِه | resembling | S | similar | resembling/resemble (1461 vs 421) |
| 1466 | مَشْهُود | attested | S | acknowledged | attest/attested (1961 vs 1466) |
| 1467 | مُشَيَّدَة | erected | S | constructed | erect (1166) and erected (1467) |
| 1471 | مُصَدِّق | confirmer | S | endorser | confirmation (363) and confirmer (1471) |
| 1472 | مَصْرِف | avoidance | S | bypass | avoid (44) and avoidance (1472) |
| 1480 | مُضارّ | injurious | S | hurtful | injurious/injury (1480 vs 943) |
| 1487 | مُطَّلِع | overlooking | S | surveying | overlook (1097) and overlooking (1487) |
| 1490 | مُطَهَّرَة | cleansed | S | spotless | cleansed/cleansing (1490 vs 973) |
| 1508 | مُعَطَّلَة | deserted | S | vacant | deserter (641), deserted (1508), desertion (1762): three forms |
| 1533 | مُفْرَط | forgotten | S | disregarded | forgotten/forget (1533 vs 492); also مفرط = neglected/abandoned, not forgotten |
| 1556 | مَقْسُوم | portioned | S | allocated | portion (315) and portioned (1556) |
| 1578 | مِلْء | fullness | S | plenitude | full (1221) and fullness (1578) |
| 1619 | مُنقَعِر | toppled | S | felled | toppled (1619) and topple (1819); toppled (1619) with topple (1819) |
| 1620 | مُنقَلَب | final | S | upshot | final (1620) and finalize (1894) |
| 1632 | مُهْتَدي | enlightened | S | righteous | enlightenment (348) and enlightened (1632) |
| 1641 | مَوْبِق | doom | S | pitfall | doomed (347) and doom (1641) |
| 1647 | مُؤْصَدَة | enclosed | S | bolted | enclosed (1647) and enclose (1832) |
| 1680 | نائِم | sleeper | S | dormant | sleeper (1680) and sleep (1754) |
| 1692 | نَدِيّ | gathering | S | forum | gather (43) and gathering (1692) |
| 1696 | نَزّاعَة | peeler | S | plucker | peeler (1696) and peel (1706) |
| 1698 | يُسارِعُ | rush | S | scramble | rushing (1545) and rush (1698) |
| 1705 | أَسْقَيْ | water | S | irrigate | watering (1462) and water (1705) |
| 1715 | نَضْرَة | brightness | S | freshness | brighten (120) and brightness (1715) |
| 1741 | قَيَّضْ | allot | S | assign | allotment (1243) and allot (1741) |
| 1745 | كَسَوْ | clothe | S | dress | clothing (1261) and clothe (1745) |
| 1757 | هاجَرَ | emigrate | S | relocate | emigrate (1757) with emigrant/migrant (1628, 1629); emigrate (1757) with emigrant (1628) and migrant (1629) |
| 1762 | هَجْر | desertion | S | estrangement | desertion (1762) belongs to the deserter/deserted family |
| 1771 | هَمّاز | slanderer | S | detractor | slander (172) and slanderer (1771) |
| 1774 | هَوْن | humbly | S | softly | humble (78) and humbly (1774); humility (198), humiliated (897) are the same cluster |
| 1777 | أَبْقَى | enduring | S | permanent | endure (681) and enduring (1777) |
| 1783 | وارِد | arriving | S | incoming | arrive (522) and arriving (1783) |
| 1789 | أَعْتَدَتْ | prepare | S | arrange | preparation (486) and prepare (1789) |
| 1792 | إِقام | establishing | S | performing | establish (1749) and establishing (1792) |
| 1802 | إِيتاء | bestowal | S | dispensing | bestowal/bestow (1802 vs 234) |
| 1807 | تَمَّ | complete | S | conclude | complete/completion/finalize (1807 vs 482, 1894) |
| 1814 | وِجْهَة | direction | S | course | direct (1434) and direction (1814) |
| 1820 | خُفْيَة | secretly | S | privately | secrecy (115) and secretly (1820) |
| 1825 | وَزْن | balance | S | equilibrium | balanced (1551) and balance (1825) |
| 1830 | سُعُر | flaming | S | inferno | flame (888) and flaming (1830) |
| 1843 | غَرَّ | delude | S | swindle | delude/delusion/deluded (1843, 1064, 2043) |
| 1847 | وَقْر | heaviness | S | bulk | heavy (36) and heaviness (1847) |
| 1852 | وُكِّلَ | delegate | S | commission | delegation (1845) and delegate (1852) |
| 1858 | مَأْوَى | dwelling | S | homestead | dwell (121), dwellers (573), dwelling (1858): three forms |
| 1863 | يَخْلُدْ | perpetuate | S | eternalize | perpetual (685) and perpetuate (1863) |
| 1885 | تَطَهَّرْ | purify | S | refine | purify/purification (1885 vs 438) |
| 1909 | يُحْدِثُ | originate | S | innovate | originate (1909) with origination (227) and originator (1098) |
| 1919 | يَخْتَصُّ | distinguish | S | privilege | distinguished (1815) and distinguish (1919) |
| 1933 | يُرْضُ | please | S | placate | please/pleased/pleasing (1933, 1445, 1411) |
| 1943 | اسْتَعْجَلَ | hurry | S | speed | hurrying (819) and hurry (1943) |
| 1944 | اسْتَقامُ | stand | S | vertical | standing (1167) and stand (1944) |
| 1949 | يُسْر | facility | S | convenience | facility/facilitate (1949 vs 1950); facility has a building sense |
| 1959 | يَشْفَعُ | intercede | S | beseech | intercede (1959) joins intercessor (861) and intercession (879) |
| 1994 | أَفْلَحَ | prosper | S | thrive | prosperity (1724) and prosper (1994) |
| 1998 | أَقْرَضُ | loaned | S | financed | loaned/loan (1998 vs 1179) |
| 2007 | يُلْحِدُ | depart | S | digress | departure (712) and depart (2007) |
| 2023 | يَنتَظِرُ | expect | S | foresee | expectant (1398) and expect (2023) |
| 2041 | يَصِلُ | connect | S | join | connected (1838) and connect (2041) |
| 2042 | وَعَظْ | admonish | S | chide | admonition (1653) and admonish (2042) |
| 2043 | أُفِكَ | deluded | S | sidetracked | deluded (2043) with delude (1843) and delusion (1064) |
| 2045 | يُوقِنُ | certain | S | confident | certainty/certain (2001 vs 2045) |
| 60 | اخْتِلاف | difference | C | variation | difference/differ (60 vs 63): same word family |
| 145 | اضْطُرَّ | compelled | C | - | compelled/compulsion (145 vs 193) |
| 671 | خِيانَة | betrayal | C | - | betrayal/betray (671 vs 70): same word family |
| 872 | شَرْقِيّ | eastern | C | - | eastern (872) and east (1460) |
| 1023 | عِزَّة | might | C | - | might/mightier (1023 vs 159) |
| 1054 | غاوي | misguided | C | - | misguided (1054) vs misguidance (434) |
| 1158 | قاض | judge | C | - | judge/judgment (1158 vs 607) |
| 1213 | كارِه | averse | C | - | averse (1213) belongs to the aversion/avert family (157, 1501, 1864) |
| 1295 | مَأْكُول | devoured | C | - | devoured/devourer (1295 vs 190) |
| 1474 | مُصْفَرّ | yellowed | C | - | yellowed/yellow (1474 vs 921) |
| 1882 | يَتَرَقَّبُ | watchful | C | - | watchful/watch (1882 vs 403) |
| 1989 | يَغِيظُ | enrage | C | - | enrage/raging (1989 vs 451) |
| 2004 | يُلاقُ | meet | C | - | meet/meeting (2004 vs 1579) |

### vague (62)

Gloss is too vague, ambiguous or a loose fit.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 2040 | يُوزَعُ | marshalled | S | regimented | marshalled is UK spelling (marshaled in US): typing variance |
| 12 | آمّ | heading | C | - | آمّ = those heading to (the Sacred House); heading is vague |
| 72 | ادّارَكَ | overtake | C | - | ادّارك = reach/exhaust; overtake is loose |
| 92 | أُزْلِفَتْ | closer | C | - | أزلفت = brought near; closer is a comparative |
| 99 | اسْتَحَبُّ | favor | C | - | استحب = preferred/loved; favor is vague and clashes with prefer (2) |
| 224 | انسَلَخَ | slip | C | - | انسلخ = shed/slip out of; slip is vague |
| 274 | بالِغَة | conclusive | C | profound | بالغة = far-reaching; conclusive is loose |
| 345 | تَبَرُّج | display | C | - | تبرج = showing off adornment; display is vague |
| 415 | اسْتَكْثَرْ | multiply | C | - | استكثر = take much/regard as much; multiply is loose |
| 451 | تَغَيُّظ | raging | C | - | تغيظ = raging/boiling; fine but intensity word |
| 485 | تَمَنَّى | long | C | - | تمنى = wish/long for; long is also an adjective |
| 495 | أَنقَذَ | deliver | C | - | deliver is ambiguous (parcels) |
| 518 | ثَمَرَة | produce | C | - | produce is a noun/verb homograph; ثمرة = fruit |
| 565 | حارَبَ | war | C | - | حارب = fight a war; war is a noun |
| 570 | حاصِب | storm | C | - | حاصب = stone-pelting wind; storm is vague |
| 592 | حِزْب | party | C | - | party is ambiguous (celebration/political) |
| 604 | حِطَّة | absolution | C | forgiveness | حطة absolution: rare, religious |
| 610 | حَمُولَة | pack | C | - | حمولة = pack animals; pack is vague |
| 626 | خالَة | auntie | C | - | auntie is informal; خالة = maternal aunt; auntie (626, maternal aunt) vs aunt (1041, paternal aunt): near duplicates |
| 651 | خَصاصَة | want | C | - | want is ambiguous (verb); خصاصة = poverty/need |
| 738 | رَدِفَ | tail | C | - | tail as a verb is informal; ردف = follow close behind |
| 748 | رَفِيق | comrade | C | - | comrade is political; رفيق = companion |
| 799 | سامِر | nocturnal | C | - | nocturnal is loose for سامر (night-talking) |
| 846 | سُنبُلَة | spike | C | - | spike is vague; سنبلة = ear of grain |
| 935 | صَيْد | game | C | prey | game is ambiguous |
| 982 | ظَمَأ | parch | C | - | parch is a verb; ظمأ = thirst |
| 1020 | عَرَبِيّ | arabic | C | - | arabic is lowercase language/ethnic label |
| 1120 | فَجّ | pass | C | - | pass is ambiguous |
| 1128 | فِرار | flight | C | - | flight is ambiguous (aircraft/escape) |
| 1169 | قَبِيل | confrontation | C | - | قبيل confrontation: loose |
| 1174 | قَدَّمَ | forward | C | - | قدم = bring forward; forward is wrong POS |
| 1180 | قِرْطاس | parchment | C | document | parchment is loose for قرطاس (paper) |
| 1201 | قِنطار | treasure | C | bullion | قنطار = a large sum; treasure is vague (1250 riches) |
| 1246 | كَلالَة | collaterals | C | - | collaterals is a legal term, ambiguous (finance) |
| 1279 | مُتَّقي | prudent | C | - | متقي = God-fearing; prudent is a deliberate neutral choice |
| 1396 | مُرْتاب | skeptical | C | - | skeptical is US spelling (sceptical) |
| 1422 | مُزْدَجَر | warning | C | - | مزدجر = deterrence; warning clashes with warner (1607) |
| 1444 | مُسْرِف | prodigal | C | - | prodigal is Biblical |
| 1459 | مُسَوِّم | insignia | C | - | insignia is a noun; مسوِّم = marking |
| 1484 | مُضِيّ | onward | C | - | مضيّ = going on; onward is vague |
| 1493 | مَعاذ | asylum | C | - | asylum has legal sense; معاذ = refuge |
| 1546 | مُقْتَدِر | potent | C | - | potent is loose for مقتدر (powerful) |
| 1638 | مَواخِر | ploughing | C | - | ploughing is UK spelling (plowing) |
| 1671 | ناصِح | counselor | C | - | counselor US spelling (counsellor) |
| 1695 | رَبَّيَ | rear | C | - | rear is ambiguous |
| 1767 | هَضْم | wrong | C | injustice | هضم = injustice; wrong is vague |
| 1776 | هَيْهات | improbable | C | - | هيهات = far be it; improbable is loose |
| 1859 | أَقْرَرْ | fix | C | - | أقرر = acknowledge; fix is loose |
| 1875 | بَسَطَ | outspread | C | - | outspread is an adjective |
| 1896 | تَوَفَّى | recall | C | - | توفى = cause to die/take the soul; recall is euphemistic |
| 1934 | يَرْغَبُ | covet | C | - | رغب = desire; covet is Biblical and negative |
| 1936 | أَرادَ | aim | C | - | أراد = want/intend; aim is loose |
| 1937 | زالَت | lapse | C | - | زال = cease/pass away; lapse is loose |
| 1942 | اسْتَضْعَفُ | belittle | C | - | استضعف = deem weak/oppress; belittle is loose |
| 1970 | أَضَلَّ | derail | C | misdirect | derail has a train sense; أضل = lead astray |
| 1978 | يَعْبَؤُا۟ | mind | C | - | mind is a noun |
| 1982 | عَصا | staff | C | baton | staff is ambiguous (employees) |
| 1995 | يَقْبَلُ | embrace | C | - | embrace is loose; يقبل = accept (1892) |
| 2008 | يَلْعَبْ | sport | C | - | sport is a noun; lab (1273 play) |
| 2028 | يُنظَرُ | adjourn | C | - | adjourn is a legal term; ينظر = be given respite |
| 2029 | يَنْعِقُ | holler | C | shriek | holler is informal |
| 2032 | انقَلَبَ | flip | C | - | flip is informal |

### archaic (40)

Archaic, literary or very formal wording.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 39 | اثْنَيْن | twain | S | dual | twain is archaic and forced; two/twain are confusable to a reader |
| 184 | اقْتَرَبَ | nigh | S | imminent | nigh is archaic and rhymes with high/sigh; اقترب = draw near |
| 216 | نَبَأ | tidings | S | update | tidings is archaic; نبأ means significant news/report |
| 322 | بُهْتان | calumny | S | libel | calumny is rare/literary and overlaps slander cluster (172, 61, 177); بهتان = slanderous lie |
| 327 | والِدَي | sire | S | begetter | sire is archaic, also a verb, and duplicates parents (24); والد = father/parent |
| 405 | رَأَى | behold | S | observe | behold is archaic; رأى = see |
| 988 | عابِد | servitor | S | believer | servitor is archaic; عابد = worshipper; 987/988 are gender variants of one lemma |
| 1059 | غَدَ | morn | S | tomorrow | morn is archaic; غدا = go out in the morning |
| 1138 | صَعِقَ | swoon | S | stupefied | swoon is archaic; صعق = be struck unconscious |
| 1139 | فِضَّة | argent | S | silver | argent is heraldic/archaic for silver; فضة = silver |
| 1361 | مُحْضَر | haled | S | brought | haled is archaic; محضر = brought/presented |
| 1437 | مُسْتَمِع | hearkening | S | attentive | hearkening is archaic |
| 1685 | تَرَبَّصْ | bide | S | linger | bide is archaic |
| 1688 | جَزَى | requite | S | reimburse | requite is rare and pairs with requital (1340), repay (1687) |
| 1854 | أَمَة | handmaid | S | maidservant | handmaid is archaic; أمة = female servant/slave |
| 1856 | وَلِيد | stripling | S | toddler | stripling is archaic; وليد = young boy |
| 1867 | وَيْ | alas | S | ah | alas is an archaic interjection; وي = an exclamation of surprise |
| 2021 | يَنۢبَغِي | behoove | S | befit | behoove is archaic |
| 291 | بَرَكَة | boon | C | - | boon is dated; بركة = blessing |
| 309 | بُعْثِرَ | upheaved | C | unearthed | upheaved is dated |
| 521 | ثَيِّبَة | matron | C | - | matron is dated; ثيبة = previously married woman |
| 615 | حِيلَة | stratagem | C | trick | stratagem is formal |
| 704 | ذِراع | cubit | C | - | cubit is archaic; ذراع = arm/forearm length |
| 740 | رَشَد | rectitude | C | integrity | rectitude is formal/hard to spell |
| 741 | رِضْوان | approbation | C | goodwill | approbation is rare; رضوان = approval/pleasure |
| 754 | رَهْبَة | trepidation | C | unease | trepidation is long/formal |
| 881 | شِقاق | schism | C | dissension | schism is formal |
| 894 | صادِق | veracious | C | honest | veracious is literary |
| 917 | صَغار | abasement | C | indignity | abasement is rare |
| 950 | ضَنك | straitened | C | cramped | straitened is dated |
| 1030 | عَطاء | largesse | C | generosity | largesse is rare |
| 1087 | فاجِر | dissolute | C | - | dissolute is formal |
| 1502 | مَعَرَّة | opprobrium | C | stigma | opprobrium is formal |
| 1527 | مَفاز | felicity | C | windfall | felicity is formal |
| 1617 | مُنفَطِر | sundered | C | - | sundered is archaic |
| 1653 | مَوْعِظَة | admonition | C | - | admonition is formal |
| 1710 | نُشُوز | recalcitrance | C | - | recalcitrance is long/formal |
| 1752 | يَنسَخُ | abrogate | C | supersede | abrogate is formal |
| 1769 | هَلُمّ | hither | C | - | hither (1769) is archaic; هلمّ = come here, and هاؤم = here |
| 1813 | وَجِل | timorous | C | cowering | timorous is rare |

### sensitive (39)

Offensive, violent, sexual, insulting or religiously loaded in a random phrase.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 1172 | قَتُور | niggardly | M | tightfisted | niggardly looks like a slur and has caused offence; قتور = stingy |
| 528 | جاهِل | fool | S | uneducated | fool is an insult inside a random phrase; جاهل = ignorant person |
| 529 | جائِر | deviant | S | wrongful | deviant carries a sexual/criminal tone; جائر = unjust, swerving from right; deviant (529) and deviate (764) |
| 1004 | عَبْد | slave | S | retainer | slave is a loaded word in a random phrase |
| 1013 | عَجُوز | crone | S | granny | crone is insulting; عجوز = old woman |
| 1666 | مِيعاد | tryst | S | juncture | tryst means a lovers' meeting; ميعاد = appointed time |
| 1865 | يَسْفِكُ | bloodshed | S | bleed | bloodshed is violent and the wrong POS; يسفك = shed (blood) |
| 186 | أَقْسَمُ | swear | C | - | swear can read as profanity; أقسم = take an oath |
| 221 | انتِقام | vengeance | C | reprisal | vengeance is violent-sounding |
| 307 | بَطْش | assault | C | grip | assault sounds violent in a random phrase |
| 398 | راوَدُ | solicit | C | - | solicit reads as sexual/commercial; راود = try to persuade or seduce |
| 412 | يَسُبُّ | revile | C | insult | revile is formal and insult-related |
| 439 | تَظاهَرُ | collude | C | - | collude has a criminal tone; تظاهر = support each other |
| 463 | تَقْتِيل | slaughter | C | - | slaughter is violent |
| 665 | خَلِيل | intimate | C | - | intimate can read sexual; خليل = close friend |
| 669 | خِنزِير | swine | C | pig | swine is an insult and religiously loaded |
| 792 | ساحِر | sorcerer | C | magician | sorcerer is occult/religiously loaded |
| 824 | سَفاهَة | foolishness | C | - | simpleton (828) is an insult; foolishness/folly (824, 826) near-synonyms |
| 828 | سَفِيه | simpleton | C | reckless | simpleton is an insult |
| 834 | سِكِّين | knife | C | - | knife is a weapon word |
| 848 | قُتِّلُ | massacre | C | - | massacre is violent |
| 850 | سَوْءَة | nakedness | C | - | nakedness is sexual-adjacent |
| 925 | صَلَبُ | crucify | C | - | crucify is religiously loaded and violent |
| 1029 | عُصْبَة | gang | C | troop | gang is negative; عصبة = band/group |
| 1033 | عِفْرِيت | demon | C | ogre | demon is religiously loaded |
| 1048 | عَوْرَة | exposed | C | - | exposed/عورة = vulnerable or private parts |
| 1057 | غُثاء | scum | C | froth | scum is offensive-sounding |
| 1079 | غَنِمْ | plunder | C | acquire | plunder is violent; غنم = take as gain |
| 1141 | عَقَرَ | hamstring | C | - | hamstring is violent |
| 1214 | كَأْس | chalice | C | - | chalice is religiously loaded |
| 1345 | مَجْنُون | crazed | C | deranged | crazed insults; مجنون = possessed/mad |
| 1407 | مِرْصاد | ambush | C | - | ambush is violent |
| 1425 | مُسافِحَة | debauched | C | - | debauched/libertine (1425, 1426) are sexual-adjacent; gender variants of one lemma |
| 1548 | مُقْتَرِف | perpetrating | C | - | perpetrating has a criminal tone |
| 1603 | مُنتَقِم | avenger | C | - | avenger is violent |
| 1606 | مُنَجُّو | savior | C | - | savior is religiously loaded; US spelling |
| 1661 | مَيْتَة | carrion | C | - | carrion is grisly |
| 1799 | مَيْسِر | gambling | C | - | gambling is sensitive |
| 1833 | سَيِّد | master | C | - | master pairs with slave (1004) |

### arabic-duplicate (37)

Different Arabic entries with identical true meaning (gender/number variants, same-root pairs); no English fix exists, the Arabic list needs a different lemma or the pair is accepted.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 264 | بازِغَة | gleaming | S | luminous | بازغة is the feminine of بازغ (263): same meaning (rising, of sun/moon); gleaming is wrong |
| 708 | ذُلُل | tractable | S | - | ذلل (plural) and ذلول (709) are the same lemma ذَلُول; tractable/tamed hide an identical meaning |
| 997 | عالِي | elevated | S | - | عالي x3 (997, 998, 999) are one lemma (high) in three forms; elevated/superior/uppermost hide identical meaning |
| 1077 | غَمْرَة | flood | S | - | غمرة (1077) and غَمَرَة (1076) are one word; flood/depths hide identical meaning |
| 1230 | جَهْر | overtly | S | - | جهر overtly: third word for "openly" (with 556, 557) |
| 1358 | مُحْصَنَة | protected | S | immune | محصنة/محصَّنة (1358, 1359) protected/fortified: same meaning; protection (1096) and protected (1358) |
| 1529 | مُفْتَر | fabricator | S | - | فري cluster: fabricator, coiner, concocted, contrived, fabrication, invent, slander (1529-1532, 61, 172, 173): seven entries for one idea |
| 1842 | عَلانِيَة | openness | S | - | علانية openness: fourth word for openly (with 556, 557, 1230) |
| 2002 | كادَ | almost | S | - | كاد (2002) and كيد (1231) both = almost; almost/nearly hide a true duplicate |
| 2033 | نَكَحَ | wed | S | - | marry/wed (241, 2033) and wedlock (1742): one idea three times |
| 10 | آمِنَة | secure | C | - | آمنة/آمن (10, 11) are gender variants with the same meaning; secure/safe is near-synonym |
| 261 | بارِزَة | conspicuous | C | - | بارزة/بارز (261, 262) gender variants; conspicuous/emerging hide identical meaning |
| 532 | جِدار | wall | C | - | جدار wall / جدر rampart: same lemma singular/plural |
| 556 | جِهار | openly | C | - | جهار/جهرة openly/publicly: same root, identical meaning |
| 709 | ذَلُول | tamed | C | - | see 708 |
| 775 | زِلْزال | earthquake | C | - | زلزال/زلزلة earthquake/tremor: same root, same meaning |
| 871 | شِرْعَة | statute | C | - | شرعة/شريعة statute/code: same root and meaning |
| 891 | صابِر | patient | C | - | صابر/صابرة patient/steadfast: gender variants |
| 919 | صَغِير | small | C | - | صغيرة minor/صغير small: gender variants |
| 921 | صُفْر | yellow | C | - | صفراء golden/صفر yellow: gender variants; golden not literal |
| 945 | ضِعاف | feeble | C | - | ضعاف feeble (plural of ضعيف 947 frail) |
| 991 | عاصِف | gale | C | - | عاصف/عاصفة gale/tempest: gender variants |
| 1000 | عامِل | worker | C | - | عامل/عاملة worker/laboring: gender variants |
| 1103 | فاكِه | merry | C | - | فاكه/فكه merry/cheerful: same root and meaning |
| 1166 | قائِم | erect | C | - | قائمة standing/قائم erect: gender variants |
| 1197 | قَلِيل | scant | C | - | قليل scant/قليلة meager: gender variants |
| 1221 | كامِلَة | full | C | - | كاملة full/كاملين entire: variants |
| 1300 | مُبارَك | fortunate | C | - | مبارك fortunate/مباركة auspicious: gender variants |
| 1337 | مُثْقَلَة | laden | C | - | مثقلة laden/مثقل encumbered: gender variants |
| 1353 | مُحَرَّمَة | taboo | C | - | محرمة taboo/محرم prohibited: gender variants |
| 1488 | مُطْمَئِنّ | reassured | C | - | مطمئن reassured/مطمئنة peaceful: gender variants |
| 1494 | مَعاش | subsistence | C | - | livelihood x4: معاش subsistence, معايش sustenance, معيشة living, عيشة livelihood (1494, 1495, 1515, 1050) are one word family with identical meaning |
| 1512 | مَعْلُوم | determinate | C | - | معلوم determinate/معلومة prescribed: gender variants |
| 1550 | مُقْتَصِد | temperate | C | - | مقتصد temperate/مقتصدة balanced: gender variants |
| 1571 | مُكَرَّمَة | esteemed | C | - | مكرمة esteemed/مكرم venerated: gender variants |
| 1612 | مُنَشَّرَة | unrolled | C | - | منشرة unrolled/منشور unfurled: variants |
| 1868 | يابِس | dry | C | - | يابسة arid/يابس dry: gender variants |

### wrong-gloss (37)

Gloss is wrong, misleading or the wrong part of speech for the lemma.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 172 | افْتِراء | slander | S | hoax | افتراء is fabricating a lie (esp. against God); slander is defamation of a person |
| 237 | انفَطَرَتْ | cleave | S | rupture | cleave is a contronym (join/split) and duplicates split (230); انفطر = burst/split open |
| 245 | أَهْدَى | guidance | S | wiser | أهدى is comparative "better guided"; guidance is a different word (هدى) and loses the sense |
| 467 | يَقْرَبُ | adjoin | S | venture | يقرب = come near; adjoin means share a border |
| 909 | صِدِّيق | sincere | S | saint | صدّيق = truthful/righteous person; sincere is an adjective, and 910 sincerity is wrong POS and a gender variant |
| 910 | صِدِّيقَة | sincerity | S | candid | صدّيقة = truthful woman; sincerity is an abstract noun (wrong), clashes with sincere (909) |
| 926 | صَلَحَ | fitting | S | healthy | صلح = be righteous/good; fitting is misleading |
| 1102 | فاكِهَة | dessert | S | luxury | فاكهة = fruit; dessert is wrong, and fruit/fruits already at 517/1150 |
| 1104 | فالِق | cleaver | S | breaker | فالق = splitter (of grain); cleaver is a butcher's knife |
| 1106 | أَنسَى | obliterate | S | eclipse | أنسى = cause to forget; obliterate is much too strong (and 492 forget) |
| 1360 | مُحْصِن | continent | S | abstinent | محصن = chaste/protected; continent means a landmass in English |
| 1514 | مُعَمَّر | veteran | S | centenarian | معمر = long-lived person; veteran implies military service |
| 1530 | مُفْتَري | coiner | S | counterfeiter | مفتري = one who fabricates a lie; coiner means a counterfeiter of coins |
| 1775 | هَيْتَ | quick | S | signal | هيت (هيت لك) = come here!; quick is wrong; come is already 441 |
| 1794 | والِدَة | dam | S | mama | والدة = mother; dam means a female animal parent or a barrier, and mother is 212 |
| 1870 | يَأْتَلِ | forswear | S | default | forswear means renounce or perjure; يأتل = swear off / fall short |
| 1913 | حَلَفْ | adjure | S | assert | adjure means urge solemnly; حلف = swear an oath |
| 34 | أَثام | guilt | C | penalty | أثام in 25:68 is penalty; guilt vs sin(37) vs sinful(3) vs wicked(40) |
| 65 | إِخْراج | expulsion | C | - | أخرج = bring out; extract is too narrow (66 vs 65 expulsion) |
| 238 | أَنقَضَ | weigh | C | - | أنقض = weighed down/broke; weigh loses the sense |
| 281 | بَحِيرَة | dromedary | C | notched | بحيرة = a camel with slit ear; dromedary loses the sense; dromedary (281) would clash with dromedaries if 548 is changed |
| 286 | بَراءَة | immunity | C | discharge | براءة = disavowal/discharge; immunity misleads |
| 389 | يَدَّعُ | pretend | C | - | ادّعى = claim; pretend implies falsehood |
| 568 | حاش | absolutely | C | - | حاش = exclamation of dissociation (far be it); absolutely misleads |
| 636 | خَبال | mischief | C | - | خبال = ruin/corruption; mischief is mild |
| 648 | خُسْران | damage | C | - | خسران = loss/ruin; damage is off, and loss is already used (381) |
| 759 | رَواسِي | anchors | C | pillars | رواسي = firm mountains; anchors is figurative |
| 836 | سُلالَة | essence | C | - | سلالة = extract/strain; essence is loose |
| 918 | صَغَتْ | lend | C | - | صغا = incline (of hearts); lend is odd |
| 1042 | عَمّ | paternal | C | - | عمّ = paternal uncle; paternal is an adjective |
| 1083 | غَيابَت | dungeon | C | bottom | غيابة = depths (of the well); dungeon is off |
| 1434 | مُسْتَقِيم | direct | C | - | مستقيم = straight (the "straight path"); direct loses this |
| 1447 | مَسْغَبَة | gnawing | C | - | مسغبة = hunger; gnawing is a participle, hunger used at 369 |
| 1540 | مَقالِيد | vaults | C | - | مقاليد = keys/treasuries; vaults is a loose fit |
| 1542 | مَقامِع | hooks | C | rods | مقامع = iron rods/maces; hooks is off |
| 1947 | اسْتَوَى | level | C | - | استوى = settle/be established; level is off |
| 1997 | قَدَرَ | enable | C | - | قدر = measure/decree/be able; enable is off |

### forced-synonym (35)

Near-synonym pair/cluster created only to keep entries unique; not a spelling clash.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 131 | شُحّ | stingy | C | - | stingy/miserliness (131 vs 272) near-synonyms |
| 142 | أَصْنام | idols | C | - | idols/effigies (142 vs 252): same Arabic sense; effigies is odd |
| 355 | تَتْبِيب | perdition | C | - | perdition/annihilation (355, 356): both = destruction |
| 360 | يَتَكَبَّرُ | haughty | C | - | haughty/arrogant (360 vs 108): same root, near-synonyms |
| 364 | تَثْرِيب | rebuke | C | - | rebuke/scold (364 vs 497) |
| 401 | تَرْجُفُ | quake | C | - | quake/quiver/shiver/shake/sway (401, 498, 469, 488, 487): one cluster of shaking verbs |
| 454 | تَفْتَؤُا۟ | desist | C | - | desist/cease (454 vs 19): both are the "never cease" verbs |
| 512 | ثَقُلَتْ | weighty | C | - | ponderous (513) vs heavy (36) vs weighty (512): ثقل cluster, ponderous is negative |
| 621 | خاشِع | submissive | C | - | submissive/bowing/humble/lowly (621, 623, 78, 676): خشع/خضع/ذلل cluster |
| 627 | خالِد | everlasting | C | - | everlasting/perpetual (627 vs 685) |
| 652 | خَصْم | adversary | C | - | adversary/contentious/disputant (652-654): one root three words |
| 798 | سامِد | heedless | C | - | heedless/unmindful (798, 800) near-synonyms; heed (719) |
| 942 | ضَرّ | detriment | C | - | ضرر cluster: harm, detriment, injury, hardship, harmful (80, 942, 943, 200, 301) |
| 970 | طَلَعَت | dawn | C | - | dawn/sunrise (970 vs 972) |
| 1011 | عَجَب | wonder | C | - | wonder/marvel/amazing (1011, 445, 1015): عجب cluster |
| 1034 | عُقْبَى | consequence | C | - | consequence/outcome (1034 vs 994) |
| 1096 | اسْتَعِذْ | protection | C | - | protection/refuge (1096 vs 165): both = seek refuge; verb glossed as a noun |
| 1111 | فَتَحَ | unlock | C | - | unlock/open (1111 vs 453) |
| 1115 | فَتَنُ | ordeal | C | - | ordeal/tribulation (1115, 1116) |
| 1126 | ذاقُ | savor | C | - | savor/taste (1126 vs 77); savor is US spelling (savour) |
| 1192 | قَطَّعَ | sever | C | - | sever/cut (1192 vs 1290) |
| 1306 | مُبَشِّر | announcer | C | - | announcer/herald/announce (1306, 299, 6) |
| 1427 | مَسْكَن | residence | C | - | residence/abode/lodging cluster (1427, 678, 1341) |
| 1435 | مُسْتَكْبِر | proud | C | - | proud/arrogant/haughty (1435, 108, 360): كبر cluster |
| 1500 | مَعْذِرَة | justification | C | - | justification (1500) vs excuse (1019) |
| 1581 | مُلْتَحَد | hideout | C | - | hideout/bolthole/recourse/refuge group (1581, 1660, 1582) |
| 1780 | اخْتارَ | elect | C | - | elect/select (1780 vs 387) |
| 1829 | وَسَط | center | C | - | center/middle (1829 vs 255); center is US spelling (centre) |
| 1839 | طَفِقَ | commence | C | - | commence/begin (1839 vs 1874) |
| 1872 | بَدَّلَ | exchange | C | - | بدل cluster: exchange, substitute, swap, replacement, alteration (1872, 1873, 1939, 97, 342) |
| 1927 | يَدُعُّ | rebuff | C | - | rebuff/thrust (1927 vs 692) |
| 1971 | أَضاعُ | waste | C | - | waste/wastefulness/squander (1971, 344, 343) |
| 2012 | يَلْمِزُ | defame | C | - | defame/slanderer/slander cluster (2012, 1771, 172) |
| 2015 | يَمْحَقُ | erase | C | - | erase/expunge (2015 vs 2016) |
| 2047 | يَئِسَ | disheartened | C | - | despair/despondent/forlorn/disheartened (112, 1877, 1311, 2047) |

### odd-word (16)

Not a standard single English word, or rare enough to be a typing hazard.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 1412 | مُرْضِعَة | wetnurse | M | lactating | wetnurse is not a standard English word (wet nurse); 87 already glosses the verb as nurse |
| 1561 | مُقْمَح | stiffnecked | M | rigid | stiffnecked is not a standard one-word form (stiff-necked) |
| 1193 | قِطْمِير | pellicle | S | membrane | pellicle is obscure; قطمير = thin skin on a date stone |
| 1463 | مُشْرِق | sunrising | S | sunup | sunrising is not standard English (sunrise) |
| 1694 | نَذِير | cautioner | S | harbinger | cautioner is rare; منذر and نذير both mean warner |
| 1992 | أَفْسَدُ | vitiate | S | undermine | vitiate is rare/formal |
| 424 | يَشَّقَّقُ | fissure | C | crack | fissure used as a verb is unusual |
| 432 | تَضْحَى | sunned | C | - | sunned is unusual; تضحى = be exposed to the sun |
| 845 | سَمُوم | simoom | C | - | simoom is obscure |
| 1165 | قائِل | sayer | C | - | sayer is rare; saying (1208) |
| 1233 | كِذّاب | mendacity | C | - | mendacity is rare |
| 1388 | مُدْهِن | temporizer | C | appeaser | temporizer is rare |
| 1492 | مُعاجِز | thwarting | C | - | defeater (1497) is odd; thwarting/defeater are one idea |
| 1509 | مُعَقِّب | reverser | C | - | reverser is odd |
| 1516 | مَغارَة | grottoes | C | hideaways | grottoes is plural of an uncommon word |
| 1826 | وَزَنُ | heft | C | - | heft is informal as a verb |

### confusable (15)

One-letter-apart, near-homophone or homograph risks beyond the plain families.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 1008 | عَبُوس | grim | S | sullen | grim (1008) and grimy (1326) differ by one letter and are unrelated in meaning; grim (1008) and grimy (1326): one letter apart, unrelated meanings |
| 1629 | مُهاجِرَة | migrant | S | newcomer | emigrant/migrant (1628, 1629): near-identical spelling and meaning; gender variants of one lemma; migrant/emigrant (1628, 1629): near-identical spe... |
| 1837 | وَصَّى | enjoin | S | instruct | enjoy (109) and enjoin (1837): near-spelling, different meaning |
| 118 | أَسَّسَ | found | C | - | found is also the past tense of find |
| 411 | تُساقِطْ | drop | C | - | shed is a homograph (noun); 418 تسقط shed vs 411 drop same root |
| 515 | ثَمانِي | eight | C | - | eight (515) and eighty (516) differ by one letter |
| 662 | خَفِيف | slight | C | - | light (661) vs slight (662) vs lightening (384) |
| 703 | ذُباب | fly | C | - | fly is also a verb |
| 1016 | عَداوَة | enmity | C | - | enmity vs enemy (1017) |
| 1207 | قَوْسَيْن | bows | C | - | bows is a homograph (bow/bow) and plural |
| 1291 | لِينَة | palm | C | - | palm is a homograph (hand) |
| 1329 | مُتَصَدِّع | rent | C | fractured | rent is a homograph (payment) |
| 1547 | مُقْتَدي | emulator | C | - | emulator has a computing sense |
| 1793 | أَكْواب | tumblers | C | beakers | tumblers also means acrobats |
| 1968 | يُضاعِفُ | compound | C | - | compound is a noun/verb homograph |

### plural-pair (14)

Singular and plural of the same English word both in the list (certain).

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 64 | أَخْدان | companions | M | partners | companions (64) is the plural of companion (1272) |
| 196 | أَكْنان | shelters | M | recesses | shelters (196) is the plural of shelter (15) |
| 548 | جِمالَت | camels | M | dromedaries | camels (548) is the plural of camel (1678) |
| 588 | حَرَس | guards | M | sentinels | guards (588) is the plural of guard (605) |
| 750 | رِكاب | mounts | M | steeds | mounts (750) is the plural of mount (974) |
| 898 | صافَّة | ranks | M | lines | ranks (898) is the plural of rank (690) |
| 1076 | غَمَرَة | depths | M | turmoil | depths (1076) is the plural of depth (691) |
| 1117 | فَتَيان | youths | M | juveniles | youths (1117) is the plural of youth (1275); lads (1119) is a third word for the same plural |
| 1150 | فَواكِه | fruits | M | harvest | fruits (1150) is the plural of fruit (517) |
| 1276 | فَسَدَتِ | spoil | M | rot | spoil (1276) and spoils (1518): same lexeme, different Arabic meanings (rot vs booty) |
| 1595 | مَنازِل | lodgings | M | mansions | lodgings (1595) is the plural of lodging (1341) |
| 1639 | مَواضِع | positions | M | sites | positions (1639) is the plural of position (1565) |
| 1822 | رُباع | fours | M | quartets | fours (1822) is the plural of four (81) |
| 1119 | فِتْيَة | lads | S | teenagers | lads (1119) is British slang and the plural of lad; فتية/فتيان/فتى are one lemma in three forms |

### homophone (6)

Two entries that sound alike (certain).

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 279 | بَحْر | sea | M | ocean | sea (279) and see (21) are homophones; the Arabic words (بحر, أبصر) are unrelated |
| 320 | بُنَىّ | son | M | junior | son (320) and sun (884) are homophones and one letter apart |
| 384 | تَخْفِيف | lightening | M | reduction | lightening (384) and lightning (1804): near-homophones one letter apart |
| 927 | صَلْد | bare | M | bald | bare (927) and bear (48) are homophones; صلد = hard bare rock |
| 1645 | مَوْزُون | weighed | M | calibrated | weighed (1645) and wade (1691) are homophones; also weigh/weight/weighty family (238, 1336, 512) |
| 1818 | وَحِيد | lone | M | only | lone (1818) is a homophone of loan (1179) and one letter from alone (1816); وحيد and وحد have the same meaning |

### long-word (2)

Long word, spelling errors likely.

| # | Lemma | Current | Sev | Proposed | Reason |
|---|---|---|---|---|---|
| 590 | حُرُمَة | inviolability | C | sanctity | inviolability: 13 letters, hard to spell |
| 1795 | رُمّان | pomegranate | C | - | pomegranate: 11 letters, spelling errors likely |

## Appendix: all edit-distance-1 pairs (373)

Format: `index word / index word`. Pairs of words up to 4 letters are the T4 problem set.

```
5 ear / 48 bear ; 5 ear / 57 fear ; 5 ear / 68 err
5 ear / 191 earn ; 5 ear / 312 far ; 5 ear / 335 eat
5 ear / 565 war ; 5 ear / 1191 tar ; 5 ear / 1695 rear
6 announce / 1306 announcer ; 11 safe / 1751 save ; 12 heading / 878 healing
15 shelter / 196 shelters ; 18 test / 273 best ; 18 test / 1397 rest
18 test / 1520 west ; 19 cease / 1664 ease ; 20 sight / 181 fight
20 sight / 515 eight ; 20 sight / 661 light ; 20 sight / 662 slight
20 sight / 954 tight ; 20 sight / 1023 might ; 21 see / 279 sea
21 see / 338 seek ; 23 door / 1099 doer ; 23 door / 1641 doom
31 lean / 1179 loan ; 31 lean / 1824 leaf ; 34 guilt / 1312 built
37 sin / 320 son ; 37 sin / 470 sit ; 37 sin / 546 skin
37 sin / 884 sun ; 37 sin / 1095 win ; 44 avoid / 579 void
48 bear / 57 fear ; 48 bear / 1695 rear ; 49 eager / 1198 meager
53 count / 974 mount ; 56 dream / 385 dread ; 57 fear / 312 far
57 fear / 1695 rear ; 62 mingle / 1779 single ; 64 companions / 1272 companion
75 repel / 966 rebel ; 77 taste / 1971 waste ; 78 humble / 1774 humbly
79 remove / 1190 remote ; 79 remove / 1215 remover ; 80 harm / 429 warm
81 four / 174 pour ; 81 four / 639 foul ; 81 four / 1822 fours
84 foot / 528 fool ; 84 foot / 965 food ; 88 ride / 229 rise
88 ride / 1610 rite ; 88 ride / 1685 bide ; 92 closer / 619 loser
99 favor / 1126 savor ; 117 swift / 1891 shift ; 121 dwell / 197 well
121 dwell / 1929 swell ; 135 month / 179 mouth ; 152 serve / 410 swerve
161 give / 666 five ; 166 scoop / 1781 stoop ; 167 drown / 1007 frown
176 reach / 1737 breach ; 179 mouth / 1275 youth ; 181 fight / 515 eight
181 fight / 661 light ; 181 fight / 954 tight ; 181 fight / 1023 might
181 fight / 1128 flight ; 184 nigh / 863 high ; 185 nearer / 609 bearer
189 pen / 453 open ; 189 pen / 728 men ; 189 pen / 1027 ten
190 devourer / 1295 devoured ; 192 most / 743 moist ; 192 most / 939 lost
197 well / 532 wall ; 197 well / 1929 swell ; 197 well / 1954 will
202 wish / 613 fish ; 207 withhold / 1592 withheld ; 210 stay / 487 sway
210 stay / 1800 star ; 212 mother / 1084 other ; 224 slip / 827 ship
224 slip / 2032 flip ; 226 raise / 229 rise ; 226 raise / 280 praise
228 revive / 412 revile ; 229 rise / 1610 rite ; 232 speak / 1740 speck
236 disperse / 1332 dispersed ; 237 cleave / 1104 cleaver ; 238 weigh / 1336 weight
241 marry / 373 carry ; 241 marry / 1103 merry ; 248 tender / 1609 sender
255 middle / 2005 muddle ; 263 rising / 722 raising ; 266 towering / 625 lowering
267 vain / 1238 gain ; 267 vain / 1485 rain ; 268 distance / 769 distanced
273 best / 326 beast ; 273 best / 1397 rest ; 273 best / 1520 west
276 pledge / 757 pledged ; 279 sea / 961 seal ; 279 sea / 1539 seat
283 report / 1334 resort ; 287 hail / 617 fail ; 287 hail / 738 tail
291 boon / 1798 moon ; 297 stature / 871 statute ; 304 expeller / 1383 expelled
312 far / 468 fair ; 312 far / 565 war ; 312 far / 1191 tar
320 son / 409 sow ; 320 son / 884 sun ; 324 gift / 747 lift
326 beast / 768 blast ; 326 beast / 1460 east ; 327 sire / 1293 fire
335 eat / 602 mat ; 335 eat / 1460 east ; 335 eat / 1539 seat
337 ruin / 1485 rain ; 338 seek / 1112 meek ; 351 spare / 422 stare
351 spare / 711 share ; 359 toss / 381 loss ; 365 stir / 1800 star
367 find / 725 bind ; 367 find / 763 wind ; 367 find / 1978 mind
370 move / 1347 love ; 379 pierce / 990 fierce ; 380 lose / 381 loss
380 lose / 619 loser ; 380 lose / 939 lost ; 380 lose / 1347 love
380 lose / 1818 lone ; 381 loss / 939 lost ; 384 lightening / 1804 lightning
385 dread / 638 bread ; 387 select / 1780 elect ; 395 soil / 1225 toil
395 soil / 1276 spoil ; 396 consent / 718 content ; 399 waiting / 1602 awaiting
400 measured / 465 measure ; 403 watch / 1926 catch ; 409 sow / 1693 vow
422 stare / 711 share ; 422 stare / 1800 star ; 429 warm / 564 wary
429 warm / 565 war ; 429 warm / 1262 swarm ; 441 come / 874 code
448 blind / 725 bind ; 448 blind / 1420 blend ; 453 open / 1977 omen
455 gush / 832 hush ; 455 gush / 1698 rush ; 458 falter / 1887 alter
464 precede / 1746 recede ; 468 fair / 617 fail ; 471 forge / 492 forget
485 long / 1818 lone ; 487 sway / 1939 swap ; 488 shake / 711 share
488 shake / 1253 shape ; 497 scold / 1966 scald ; 499 fall / 532 wall
499 fall / 617 fail ; 499 fall / 1221 full ; 501 face / 1700 race
505 eased / 1664 ease ; 512 weighty / 516 eighty ; 512 weighty / 1336 weight
515 eight / 516 eighty ; 515 eight / 661 light ; 515 eight / 954 tight
515 eight / 1023 might ; 515 eight / 1336 weight ; 517 fruit / 1150 fruits
524 flowing / 1674 glowing ; 527 assembler / 1342 assembled ; 528 fool / 639 foul
528 fool / 965 food ; 529 deviant / 1009 defiant ; 530 mountain / 2022 fountain
532 wall / 1954 will ; 532 wall / 2017 walk ; 534 debate / 2036 debase
539 barren / 1349 barred ; 541 part / 592 party ; 541 part / 2013 pant
543 body / 903 boy ; 548 camels / 1678 camel ; 550 entirety / 1218 entirely
553 tend / 918 lend ; 553 tend / 1027 ten ; 562 divider / 1728 divide
563 need / 719 heed ; 563 need / 1003 needy ; 563 need / 1973 feed
564 wary / 565 war ; 564 wary / 598 weary ; 564 wary / 1712 vary
565 war / 1191 tar ; 572 nearby / 1231 nearly ; 578 grain / 1238 gain
578 grain / 1485 rain ; 588 guards / 605 guard ; 596 good / 965 food
610 pack / 985 back ; 610 pack / 1901 pick ; 616 life / 747 lift
617 fail / 738 tail ; 617 fail / 947 frail ; 619 loser / 1090 lower
625 lowering / 1219 glowering ; 628 pure / 1866 cure ; 632 collapsed / 1763 collapse
638 bread / 1022 broad ; 641 deserter / 1508 deserted ; 650 awe / 1721 ewe
651 want / 2013 pant ; 655 green / 2010 greet ; 661 light / 662 slight
661 light / 954 tight ; 661 light / 1023 might ; 661 light / 1128 flight
662 slight / 1128 flight ; 666 five / 1293 fire ; 676 lowly / 1568 slowly
686 repulsion / 906 revulsion ; 690 rank / 898 ranks ; 691 depth / 1076 depths
702 tasting / 1583 casting ; 709 tamed / 1656 timed ; 711 share / 1253 shape
715 swelling / 1714 welling ; 715 swelling / 1858 dwelling ; 716 growing / 1674 glowing
718 content / 1920 contend ; 719 heed / 1035 heel ; 719 heed / 1973 feed
725 bind / 763 wind ; 725 bind / 1978 mind ; 727 sealed / 1161 seated
728 men / 1027 ten ; 728 men / 1977 omen ; 738 tail / 1225 toil
738 tail / 1850 trail ; 744 graze / 1224 grave ; 747 lift / 1834 left
750 mounts / 974 mount ; 755 clan / 928 clay ; 756 calm / 1291 palm
763 wind / 1095 win ; 763 wind / 1978 mind ; 772 crops / 1811 cross
806 seven / 1192 sever ; 808 path / 1519 bath ; 808 path / 1642 oath
818 parting / 941 panting ; 829 cup / 1290 cut ; 832 hush / 1698 rush
847 silk / 1263 milk ; 852 black / 985 back ; 854 proper / 1994 prosper
868 severe / 1192 sever ; 884 sun / 1107 shun ; 899 singe / 1779 single
903 boy / 1708 buy ; 915 steep / 1078 sheep ; 915 steep / 1754 sleep
918 lend / 1420 blend ; 926 fitting / 1194 sitting ; 927 bare / 1082 bane
928 clay / 1273 play ; 935 game / 991 gale ; 940 gaunt / 1041 aunt
947 frail / 1850 trail ; 954 tight / 1023 might ; 957 willing / 1714 welling
960 layer / 1165 sayer ; 960 layer / 1257 player ; 961 seal / 1539 seat
964 road / 1022 broad ; 965 food / 1077 flood ; 965 food / 1719 fold
1004 slave / 1751 save ; 1008 grim / 1326 grimy ; 1011 wonder / 1880 ponder
1013 crone / 1566 prone ; 1035 heel / 1706 peel ; 1036 knot / 1984 know
1059 morn / 1798 moon ; 1060 raven / 1395 haven ; 1078 sheep / 1754 sleep
1079 plunder / 1985 blunder ; 1112 meek / 2004 meet ; 1113 stumble / 1766 stubble
1117 youths / 1275 youth ; 1126 savor / 1606 savior ; 1127 singly / 1779 single
1132 bed / 2033 wed ; 1158 judge / 1938 nudge ; 1161 seated / 1916 heated
1166 erect / 1780 elect ; 1191 tar / 1800 star ; 1200 shirt / 1891 shift
1208 saying / 1294 staying ; 1208 saying / 1732 shying ; 1216 unveiler / 1803 unveiled
1221 full / 2024 pull ; 1235 dignify / 1846 dignity ; 1238 gain / 1485 rain
1247 dog / 1993 do ; 1248 both / 1519 bath ; 1252 cave / 1751 save
1252 cave / 1808 carve ; 1276 spoil / 1518 spoils ; 1282 backbiter / 1987 backbite
1294 staying / 1729 straying ; 1317 destroyed / 1636 destroyer ; 1329 rent / 1397 rest
1341 lodging / 1595 lodgings ; 1347 love / 1818 lone ; 1360 continent / 1733 contingent
1361 haled / 1849 halted ; 1365 shaven / 1395 haven ; 1390 wavering / 1462 watering
1397 rest / 1520 west ; 1404 rejected / 2000 dejected ; 1429 covert / 1934 covet
1429 covert / 2038 cover ; 1443 rope / 1932 hope ; 1445 pleased / 1933 please
1460 east / 1664 ease ; 1460 east / 1785 vast ; 1482 elapse / 1937 lapse
1519 bath / 1642 oath ; 1551 balanced / 1825 balance ; 1565 position / 1639 positions
1619 toppled / 1819 topple ; 1628 emigrant / 1629 migrant ; 1647 enclosed / 1832 enclose
1664 ease / 2015 erase ; 1714 welling / 1858 dwelling ; 1800 star / 1976 soar
1816 alone / 1818 lone ; 1824 leaf / 1835 deaf ; 1826 heft / 1834 left
1843 delude / 1981 elude ; 1843 delude / 2043 deluded ; 1864 avert / 1957 alert
1866 cure / 2003 curb ; 1890 think / 1960 thank ; 1934 covet / 2038 cover
1967 melt / 2004 meet
```
