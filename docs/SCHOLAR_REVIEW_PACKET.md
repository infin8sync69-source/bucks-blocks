# Arabic word list: questions for the scholar

List under review: `wordlists/ar-quran-draft-2048.txt` (2048 words, each exactly as written in the Quran, modern spelling). The review sheet is `wordlists/review/ar-quran-draft-review.csv`: every entry with its dictionary form, root, count in the Quran, and a notes column.

## What has already been done

An internal review of all 2048 entries removed divine names and attributes, acts of worship and ritual, scripture and recitation words, punishment and disbelief words, formulae, divine-act verbs and liturgical expressions, and fixed spelling (`reports/lists-review/arabic-review.md`, `tools/ar_exclusions.txt`). That review is not a substitute for yours.

## What we ask the scholar to decide

1. **The 137 words below were kept on purpose** and need a yes or no: each is ordinary vocabulary taken from one context in the Quran (a named story, or a hell, paradise or Last-Day scene). Keep, or remove? Removing a word means the builder pulls the next most frequent clean word in; the tool and the review process handle that.
2. Whether a phrase of random words, written on paper and kept as a wallet backup, raises any concern of its own (disposal of paper, the words appearing next to each other by chance).
3. Whether the list may be described as "Classical Arabic words attested in the Quran" in the product, and which wording is acceptable. The research note advises against "Quranic words", "Quran seed" and "holy words".
4. Anything else in the full list (`ar-quran-draft-2048.txt`) that should go. Add comments to the `reviewer_notes` column of the sheet.

The sheet also lists about 270 further entries the internal review marked "consider" (ordinary words with a strong association); they are in `wordlists/review/arabic-proposed-changes.csv` if you want to see them.

## Category b: prophets, persons, peoples, places, narratives (25 words)

| # | Word | Quran dictionary form | Why it was flagged |
|---|---|---|---|
| 227 | أواه | أَوّاه | أواه is the epithet of Ibrahim (9:114، 11:75); also an interjection in Arabic (A11) |
| 905 | صديقا | صِدِّيق | صديقا: epithet of prophets Ibrahim, Idris (19:41, 19:56); siddiq [Quran surface count: 2] |
| 906 | صديقة | صِدِّيقَة | صديقة: epithet of Mary (5:75) [Quran surface count: 1] |
| 921 | صلبوه | صَلَبُ | صلبوه (4:157) is the crucifixion of Jesus; doctrinally sensitive [Quran surface count: 1] |
| 1354 | مثابة | مَثابَة | مثابة occurs only in 2:125 (the Ka'ba as a resort); holy place [Quran surface count: 1] |
| 1376 | محمودا | مَحْمُود | محمودا: 17:79 مقاما محمودا (the Prophet's station); from the root of excluded محمد/احمد [Quran surface count: 1] |
| 1409 | مردفين | مُرْدِف | مردفين (8:9) is the thousand angels at Badr; Badr excluded [Quran surface count: 1] |
| 1445 | مسحورا | مَسْحُور | مسحورا belongs to the cluster مستورا, مسحورا, مسرورا, مسطورا, محسورا |
| 1459 | مسومين | مُسَوِّم | مسومين (3:125) are the five thousand angels at Badr; attached [Quran surface count: 1] |
| 1513 | مغاضبا | مُغاضِب | مغاضبا occurs only in 21:87 (Yunus, ذا النون) [Quran surface count: 1] |
| 1531 | مقام | مَقام | مقام: مقام إبراهيم (2:125; 3:97) and مقام ربه [Quran surface count: 7] |
| 1560 | مكظوم | مَكْظُوم | مكظوم occurs only in 68:48 (Yunus and the whale) [Quran surface count: 1] |
| 1593 | منذر | مُنذِر | منذر is the Prophet's role (إنما أنت منذر) [Quran surface count: 5] |
| 1609 | مهاجر | مُهاجِر | مهاجر: the Hijra (29:26 Ibrahim; 4:100); a historic religious term [Quran surface count: 1] |
| 1610 | مهاجرات | مُهاجِرَة | مهاجرات (60:10) the women emigrants of the Hijra [Quran surface count: 1] |
| 1654 | ناقة | ناقَة | ناقة: the she-camel of Salih (ناقة الله, 7:73; 11:64; 26:155) [Quran surface count: 4] |
| 1670 | نذير | نَذِير | نذير is the Prophet's role (إنما أنا نذير مبين); epithet of messengers [Quran surface count: 29] |
| 1784 | والسلوى | سَلْوَى | والسلوى (2:57; 7:160; 20:80) the quails of the Israelites; attached و+ال [Quran surface count: 3] |
| 1808 | وجيها | وَجِيه | وجيها: epithet of Jesus (3:45) and Moses (33:69) [Quran surface count: 2] |
| 1831 | وسيدا | سَيِّد | وسيدا (3:39) John the Baptist as sayyid; attached و [Quran surface count: 1] |
| 1844 | وطفقا | طَفِقَ | وطفقا occurs only in 7:22 and 20:121 (Adam and Hawwa covering themselves) [Quran surface count: 2] |
| 1866 | وقفينا | قَفَّيْ | وقفينا (2:87; 5:46; 57:27) follows the prophets with Jesus [Quran surface count: 3] |
| 1868 | وكهلا | كَهْل | وكهلا occurs only in 3:46 and 5:110 (Jesus speaks 'in the cradle and as a grown man') [Quran surface count: 2] |
| 1957 | يستنكف | اسْتَنكَفُ | يستنكف occurs only in 4:172 (Jesus and the angels do not disdain being servants of God) [Quran surface count: 2] |
| 2002 | يقطين | يَقْطِين | يقطين occurs only in 37:146 (Yunus under the plant) [Quran surface count: 1] |

## Category c: last-day, hell, paradise, unseen vocabulary (112 words)

| # | Word | Quran dictionary form | Why it was flagged |
|---|---|---|---|
| 72 | أزفت | أَزِفَتِ | أزفت is the verb of الآزفة (the Hour drew near, 53:57); last-day term |
| 79 | إستبرق | إِسْتَبْرَق | إستبرق is paradise-garment vocabulary (18:31, 44:53, 55:54, 76:21: every Quranic use) |
| 153 | أغويتنى | أَغْوَيْ | أغويتنى is Iblis' accusation to God (7:16; 15:39 رب بما أغويتني); final ى for ي and attached pronoun; excluded Iblis context |
| 164 | أقتت | أُقِّتَتْ | أقتت is the time-fixing of the messengers in the Last Day sura al-Mursalat (77:11); eschatological |
| 207 | أنشره | أَنشَرَ | أنشره is the resurrection verb (80:22 ثم إذا شاء أنشره); نشور and بعث excluded |
| 220 | انكدرت | انكَدَرَتْ | انكدرت occurs only in 81:2 (إذا النجوم انكدرت), a Takwir last-day sign |
| 271 | بررة | بَرّ | بررة occurs only in 80:16 (كرام بررة), the angelic scribes; with سفرة (825) it forms one angel phrase |
| 272 | برزخ | بَرْزَخ | برزخ (barzakh) is the intermediate state after death (23:100) as well as the barrier of 55:20; eschatological term |
| 297 | بعثر | بُعْثِرَ | بعثر upheaved: بعثرت القبور (82:4، 100:9), resurrection day; بعث family excluded |
| 569 | حامية | حامِيَة | حامية is hellfire (88:4 نارا حامية; 101:11 نار حامية) [Quran surface count: 2] |
| 617 | خافضة | خافِضَة | خافضة is from 56:3 (خافضة رافعة), the Event (الواقعة, excluded) sura [Quran surface count: 1] |
| 619 | خالدين | خالِد | خالدين is nearly always خالدين فيها: abiding for ever in hell/paradise; eschatological formula [Quran surface count: 44] |
| 641 | خزنتها | خَزَنَت | خزنتها (39:71, 39:73, 67:8) are the keepers of hell/paradise, the angelic wardens [Quran surface count: 3] |
| 731 | رضوان | رِضْوان | رضوان (3:162; 3:174; 57:27) divine pleasure; Ridwan is also the name of paradise's keeper in tradition and a common name [Quran surface coun |
| 735 | رفرف | رَفْرَف | رفرف (55:76) is paradise upholstery [Quran surface count: 1] |
| 760 | زجرة | زَجْرَة | زجرة (37:19, 79:13) is the Last Day blast [Quran surface count: 2] |
| 767 | زفير | زَفِير | زفير (11:106, 25:12) is the sound of the damned in hell [Quran surface count: 2] |
| 769 | زلزلت | زُلْزِلُ | زلزلت occurs in 99:1 (إذا زلزلت الأرض زلزالها), the Last Day sura [Quran surface count: 1] |
| 770 | زلزلة | زَلْزَلَة | زلزلة occurs in 22:1 (إن زلزلة الساعة شيء عظيم); الساعة is excluded [Quran surface count: 1] |
| 773 | زمرا | زُمَر | زمرا occurs only in 39:71,73 (hell and paradise groups) [Quran surface count: 2] |
| 774 | زمهريرا | زَمْهَرِير | زمهريرا (76:13) is bitter cold, hell in tradition [Quran surface count: 1] |
| 778 | زوجت | زُوِّجَتْ | زوجت occurs in 81:7 (وإذا النفوس زوجت), the Takwir last-day verse; also modern 'I married' [Quran surface count: 1] |
| 814 | سراعا | سِراع | سراعا is in the same one-letter cluster as سرابا, سراجا, سراحا, ترابا, غرابا, سحابا |
| 816 | سرر | سُرُر | سرر: couches of paradise (every use) [Quran surface count: 5] |
| 821 | سفرة | سَفَرَة | سفرة occurs only in 80:15, the angelic scribes [Quran surface count: 1] |
| 845 | سندس | سُندُس | سندس is the paradise silk (18:31, 44:53, 76:21 only) [Quran surface count: 3] |
| 853 | سيرت | سُيِّرَتْ | سيرت (78:20, 81:3) the mountains set moving on the Last Day [Quran surface count: 2] |
| 857 | شافعين | شافِع | شافعين (26:100) intercession (Last Day) [Quran surface count: 1] |
| 875 | شفاعة | شَفاعَة | شفاعة is intercession on the Day of Judgement [Quran surface count: 6] |
| 876 | شفيع | شَفِيع | شفيع is the intercessor of the Last Day [Quran surface count: 5] |
| 883 | شهيقا | شَهِيق | شهيقا (67:7) the sound of the damned in hell (with زفير in 11:106) [Quran surface count: 1] |
| 884 | شواظ | شُواظ | شواظ (55:35) is hellfire flame [Quran surface count: 1] |
| 988 | عاملة | عامِلَة | عاملة occurs only in 88:3 (عاملة ناصبة), where ناصبة is excluded [Quran surface count: 1] |
| 1046 | غراما | غَرام | غراما is the punishment of hell (25:65); modern 'passion / gram' (A11) [Quran surface count: 1] |
| 1066 | غواش | غاشِيَة | غواش (7:41) is the covering of hellfire above the damned [Quran surface count: 1] |
| 1085 | فاقرة | فاقِرَة | فاقرة (75:25) is a calamity of the Last Day sura (القيامة) [Quran surface count: 1] |
| 1089 | فاكهين | فاكِه | فاكهين (44:27; 52:18): delighted, said of the people of paradise [Quran surface count: 2] |
| 1137 | فصعق | صَعِقَ | فصعق is the trumpet blast of the Last Day (39:68) [Quran surface count: 1] |
| 1146 | ففزع | فَزِعَ | ففزع is the trumpet terror of the Last Day (27:87) [Quran surface count: 2] |
| 1165 | قاصرات | قاصِرَة | قاصرات is the houris (قاصرات الطرف, 37:48; 38:52; 55:56) [Quran surface count: 3] |
| 1190 | قرين | قَرِين | قرين (43:36 a devil assigned as companion; 37:51) the jinn/devil companion [Quran surface count: 2] |
| 1201 | قطران | قَطِران | قطران (14:50) is the tar garments of hell [Quran surface count: 1] |
| 1225 | كاشفة | كاشِفَة | كاشفة (53:58) the Hour has none to remove it besides God [Quran surface count: 1] |
| 1228 | كالحون | كالِح | كالحون (23:104) the disfigured faces of the damned [Quran surface count: 1] |
| 1230 | كالمهل | مُهْل | كالمهل: molten brass of hell (18:29; 44:45; 70:8); attached ك and ال [Quran surface count: 3] |
| 1249 | كشطت | كُشِطَتْ | كشطت (81:11) is a Takwir last-day verse [Quran surface count: 1] |
| 1267 | لابثين | لابِث | لابثين occurs only in 78:23 (لابثين فيها أحقابا), hell [Quran surface count: 1] |
| 1270 | لاغية | لاغِيَة | لاغية (88:11) is the paradise description [Quran surface count: 1] |
| 1296 | لقاء | لِقاء | لقاء: لقاء ربهم / لقاء الله / لقاء الآخرة, meeting God on the Last Day [Quran surface count: 8] |
| 1311 | مارج | مارِج | مارج (55:15) is the jinn's fire; jinn excluded [Quran surface count: 1] |
| 1312 | ماكثين | ماكِث | ماكثين (18:3): abiding for ever in paradise; 43:77 has the hell counterpart ماكثون [Quran surface count: 1] |
| 1332 | مبلسون | مُبْلِس | مبلسون (6:44; 23:77; 43:75) the despairing, the root of إبليس (excluded) [Quran surface count: 3] |
| 1361 | مثوى | مَثْوًى | مثوى: mostly the abode in hell (النار مثواكم) [Quran surface count: 9] |
| 1374 | محضرون | مُحْضَر | محضرون: mostly those brought to the punishment/Reckoning [Quran surface count: 5] |
| 1385 | مخضود | مَخْضُود | مخضود (56:28) paradise [Quran surface count: 1] |
| 1386 | مخلدون | مُخَلَّد | مخلدون (56:17; 76:19) the immortal youths of paradise [Quran surface count: 2] |
| 1392 | مدحورا | مَدْحُور | مدحورا (7:18; 17:18, 39): Iblis' expulsion, hell [Quran surface count: 3] |
| 1405 | مرجعكم | مَرْجِع | مرجعكم: إلى الله مرجعكم / إلى ربكم مرجعكم, return to God [Quran surface count: 11] |
| 1412 | مرساها | مُرْسَى | مرساها (7:187; 79:42) are the questions about the Hour; الساعة excluded [Quran surface count: 2] |
| 1414 | مرصادا | مِرْصاد | مرصادا occurs only in 78:21 (إن جهنم كانت مرصادا) [Quran surface count: 1] |
| 1423 | مرقوم | مَرْقُوم | مرقوم (83:9,20) the record of deeds (كتاب مرقوم) [Quran surface count: 2] |
| 1430 | مزيد | مَزِيد | مزيد: 50:30, 50:35 the dialogue of hell/paradise (هل من مزيد) [Quran surface count: 2] |
| 1489 | مطهرة | مُطَهَّرَة | مطهرة: purified spouses of paradise (2:25; 3:15; 4:57); 80:14 and 98:2 the pure scrolls [Quran surface count: 5] |
| 1490 | مطويات | مَطْوِيَّة | مطويات (39:67) the heavens rolled up in His right hand on the Last Day [Quran surface count: 1] |
| 1507 | معقبات | مُعَقِّبَة | معقبات (13:11) are angels who guard [Quran surface count: 1] |
| 1519 | مغنون | مُغْني | مغنون belongs to the cluster مجنون, مسنون, مغنون, مكنون, ممنون |
| 1522 | مفازا | مَفاز | مفازا (78:31) paradise for the God-fearing [Quran surface count: 1] |
| 1523 | مفتحة | مُفَتَّحَة | مفتحة (38:50) the opened gates of the gardens of Eden [Quran surface count: 1] |
| 1532 | مقامع | مَقامِع | مقامع (22:21) the iron rods of hell [Quran surface count: 1] |
| 1535 | مقتحم | مُقْتَحِم | مقتحم (38:59) the damned troop in hell [Quran surface count: 1] |
| 1541 | مقداره | مِقْدار | مقداره (32:5; 70:4) the length of the Day [Quran surface count: 2] |
| 1546 | مقصورات | مَقْصُورَة | مقصورات (55:72) the houris in the tents [Quran surface count: 1] |
| 1558 | مكرمون | مُكْرَم | مكرمون (21:26 angels; 37:42 and 70:35 paradise) [Quran surface count: 3] |
| 1564 | ملاق | مُلاق | ملاق occurs in 69:20 (meeting my reckoning); meeting God/Reckoning [Quran surface count: 1] |
| 1565 | ملاقوا | مُلاقُوا | ملاقوا (2:46 ملاقوا ربهم; 2:249) meeting the Lord [Quran surface count: 3] |
| 1580 | ممنون | مَمْنُون | ممنون belongs to the cluster مجنون, مسنون, مغنون, مكنون, ممنون (five words one letter apart) |
| 1598 | منضود | مَنضُود | منضود (11:82 stones of sijjil, excluded; 56:29 paradise) [Quran surface count: 2] |
| 1600 | منفطر | مُنفَطِر | منفطر occurs only in 73:18 (the heaven rent on the Last Day) [Quran surface count: 1] |
| 1611 | مهانا | مُهان | مهانا occurs only in 25:69 (hell, يخلد فيه مهانا) [Quran surface count: 1] |
| 1615 | مهطعين | مُهْطِع | مهطعين (14:43; 54:8; 70:36) the Last Day [Quran surface count: 3] |
| 1619 | موبقا | مَوْبِق | موبقا occurs only in 18:52 (the abyss between idols and their worshippers on the Last Day) [Quran surface count: 1] |
| 1626 | مؤصدة | مُؤْصَدَة | مؤصدة (90:20; 104:8) the sealed fire of hell [Quran surface count: 2] |
| 1628 | موضونة | مَوْضُونَة | موضونة occurs only in 56:15 (paradise couches) [Quran surface count: 1] |
| 1632 | موفورا | مَوْفُور | موفورا occurs only in 17:63 (to Iblis, jahannam as ample recompense) [Quran surface count: 1] |
| 1634 | موقوفون | مَوْقُوف | موقوفون (34:31) the wrongdoers standing before their Lord [Quran surface count: 1] |
| 1672 | نزاعة | نَزّاعَة | نزاعة occurs only in 70:16 (hell) [Quran surface count: 1] |
| 1679 | نسفت | نُسِفَتْ | نسفت occurs only in 77:10 (Last Day sura) [Quran surface count: 1] |
| 1688 | نضاختان | نَضّاخَتان | نضاختان occurs only in 55:66 (paradise springs) [Quran surface count: 1] |
| 1689 | نضرة | نَضْرَة | نضرة (76:11; 83:24) the glow of paradise; نعيم excluded [Quran surface count: 2] |
| 1695 | نعقل | عَقَلُ | نعقل occurs only in 67:10 (the dialogue of the dwellers of the blaze) [Quran surface count: 1] |
| 1709 | نقتبس | نَقْتَبِسْ | نقتبس occurs only in 57:13 (Last Day, 'let us take light'; نور excluded) [Quran surface count: 1] |
| 1715 | نقيض | قَيَّضْ | نقيض (43:36 نقيض له شيطانا) assigns a devil; modern 'opposite' [Quran surface count: 1] |
| 1723 | نكص | نَكَصَ | نكص occurs only in 8:48 (Iblis at Badr) [Quran surface count: 1] |
| 1730 | نورث | أَوْرَثَ | نورث occurs only in 19:63 (the inheritance of paradise) [Quran surface count: 1] |
| 1744 | همزات | هَمَزَة | همزات occurs only in 23:97 (همزات الشياطين); shayatin excluded [Quran surface count: 1] |
| 1748 | هيهات | هَيْهات | هيهات (23:36) is the interjection of the disbelievers disbelieving the Resurrection; not a content word [Quran surface count: 2] |
| 1753 | واجفة | واجِفَة | واجفة occurs only in 79:8 (Last Day hearts) [Quran surface count: 1] |
| 1760 | واردون | وارِد | واردون occurs only in 21:98 (you shall enter it: hell) [Quran surface count: 1] |
| 1777 | وأكواب | أَكْواب | وأكواب (43:71; 76:15; 88:14) the goblets of paradise; attached و [Quran surface count: 3] |
| 1792 | وبرزت | بُرِّزَتِ | وبرزت (26:91; 79:36) 'hell was brought into view' [Quran surface count: 2] |
| 1857 | وفاقا | وِفاق | وفاقا occurs only in 78:26 (hell: جزاء وفاقا) [Quran surface count: 1] |
| 1858 | وفدا | وَفْد | وفدا occurs only in 19:85 (the God-fearing gathered to al-Rahman on the Last Day) [Quran surface count: 1] |
| 1865 | وقفوا | وُقِفُ | وقفوا (6:27 وقفوا على النار; 6:30 وقفوا على ربهم) the Last Day standing [Quran surface count: 2] |
| 1870 | ولدان | وَلِيد | ولدان (56:17; 76:19) the immortal youths of paradise [Quran surface count: 2] |
| 1903 | يتلقى | تَلَقَّى | يتلقى occurs only in 50:17 (the two recording angels) [Quran surface count: 1] |
| 1912 | يحاسب | حاسَبْ | يحاسب occurs only in 84:8 (Last Day reckoning) [Quran surface count: 1] |
| 1922 | يحلون | حُلُّ | يحلون (18:31; 22:23; 35:33; 43:? ) adornment of the people of paradise [Quran surface count: 4] |
| 1923 | يحمى | يُحْمَى | يحمى occurs only in 9:35 (heated in the fire of jahannam) [Quran surface count: 1] |
| 1967 | يشفع | يَشْفَعُ | يشفع: intercession (2:255; 4:85) [Quran surface count: 3] |
| 2006 | يلاقوا | يُلاقُ | يلاقوا (43:83; 52:45; 70:42) meeting their Day [Quran surface count: 3] |
| 2013 | يلفظ | يَلْفِظُ | يلفظ occurs only in 50:18 (the recording angel) [Quran surface count: 1] |
| 2030 | ينفخ | نَفَخَ | ينفخ: ينفخ في الصور, the trumpet of the Last Day [Quran surface count: 4] |
