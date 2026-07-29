// =============================================================================
// Judecoin Wallet - MnemonicWordList implementation (st-012)
//
// The English wordlist is embedded as a sorted static array. Sorted order
// matches upstream monero's english.h so isValidEnglishWord can use
// std::binary_search for O(log n) lookups.
//
// Thread-safety: the vector is built once on first call and shared across
// threads. C++11 guarantees thread-safe initialisation of function-local
// statics. The returned const reference is read-only; callers must not
// mutate it.
// =============================================================================

#include "core/wallet/MnemonicWordList.h"

#include <algorithm>
#include <QChar>

namespace judecoin_wallet::core::wallet {

// -----------------------------------------------------------------------------
// Word list (1626 lowercase ASCII words, sorted alphabetically).
// Source: monero-project/monero src/mnemonics/english.h (BSD-3-Clause).
// DO NOT EDIT MANUALLY - regenerate via tools/regen_wordlist.sh from
// upstream if a fork updates the word list.
// -----------------------------------------------------------------------------
static const std::vector<QString>& englishStorage()
{
    static const std::vector<QString> kWords = {
    QStringLiteral("abbey"), QStringLiteral("abducts"), QStringLiteral("ability"), QStringLiteral("ablaze"), QStringLiteral("abnormal"), QStringLiteral("abort"), QStringLiteral("abrasive"), QStringLiteral("absorb"),
    QStringLiteral("abyss"), QStringLiteral("academy"), QStringLiteral("aces"), QStringLiteral("aching"), QStringLiteral("acidic"), QStringLiteral("acoustic"), QStringLiteral("acquire"), QStringLiteral("across"),
    QStringLiteral("actress"), QStringLiteral("acumen"), QStringLiteral("adapt"), QStringLiteral("addicted"), QStringLiteral("adept"), QStringLiteral("adhesive"), QStringLiteral("adjust"), QStringLiteral("adopt"),
    QStringLiteral("adrenalin"), QStringLiteral("adult"), QStringLiteral("adventure"), QStringLiteral("aerial"), QStringLiteral("afar"), QStringLiteral("affair"), QStringLiteral("afield"), QStringLiteral("afloat"),
    QStringLiteral("afoot"), QStringLiteral("afraid"), QStringLiteral("after"), QStringLiteral("against"), QStringLiteral("agenda"), QStringLiteral("aggravate"), QStringLiteral("agile"), QStringLiteral("aglow"),
    QStringLiteral("agnostic"), QStringLiteral("agony"), QStringLiteral("agreed"), QStringLiteral("ahead"), QStringLiteral("aided"), QStringLiteral("ailments"), QStringLiteral("aimless"), QStringLiteral("airport"),
    QStringLiteral("aisle"), QStringLiteral("ajar"), QStringLiteral("akin"), QStringLiteral("alarms"), QStringLiteral("album"), QStringLiteral("alchemy"), QStringLiteral("alerts"), QStringLiteral("algebra"),
    QStringLiteral("alkaline"), QStringLiteral("alley"), QStringLiteral("almost"), QStringLiteral("aloof"), QStringLiteral("alpine"), QStringLiteral("already"), QStringLiteral("also"), QStringLiteral("altitude"),
    QStringLiteral("alumni"), QStringLiteral("always"), QStringLiteral("amaze"), QStringLiteral("ambush"), QStringLiteral("amended"), QStringLiteral("amidst"), QStringLiteral("ammo"), QStringLiteral("amnesty"),
    QStringLiteral("among"), QStringLiteral("amply"), QStringLiteral("amused"), QStringLiteral("anchor"), QStringLiteral("android"), QStringLiteral("anecdote"), QStringLiteral("angled"), QStringLiteral("ankle"),
    QStringLiteral("annoyed"), QStringLiteral("answers"), QStringLiteral("antics"), QStringLiteral("anvil"), QStringLiteral("anxiety"), QStringLiteral("anybody"), QStringLiteral("apart"), QStringLiteral("apex"),
    QStringLiteral("aphid"), QStringLiteral("aplomb"), QStringLiteral("apology"), QStringLiteral("apply"), QStringLiteral("apricot"), QStringLiteral("aptitude"), QStringLiteral("aquarium"), QStringLiteral("arbitrary"),
    QStringLiteral("archer"), QStringLiteral("ardent"), QStringLiteral("arena"), QStringLiteral("argue"), QStringLiteral("arises"), QStringLiteral("army"), QStringLiteral("around"), QStringLiteral("arrow"),
    QStringLiteral("arsenic"), QStringLiteral("artistic"), QStringLiteral("ascend"), QStringLiteral("ashtray"), QStringLiteral("aside"), QStringLiteral("asked"), QStringLiteral("asleep"), QStringLiteral("aspire"),
    QStringLiteral("assorted"), QStringLiteral("asylum"), QStringLiteral("athlete"), QStringLiteral("atlas"), QStringLiteral("atom"), QStringLiteral("atrium"), QStringLiteral("attire"), QStringLiteral("auburn"),
    QStringLiteral("auctions"), QStringLiteral("audio"), QStringLiteral("august"), QStringLiteral("aunt"), QStringLiteral("austere"), QStringLiteral("autumn"), QStringLiteral("avatar"), QStringLiteral("avidly"),
    QStringLiteral("avoid"), QStringLiteral("awakened"), QStringLiteral("awesome"), QStringLiteral("awful"), QStringLiteral("awkward"), QStringLiteral("awning"), QStringLiteral("awoken"), QStringLiteral("axes"),
    QStringLiteral("axis"), QStringLiteral("axle"), QStringLiteral("aztec"), QStringLiteral("azure"), QStringLiteral("baby"), QStringLiteral("bacon"), QStringLiteral("badge"), QStringLiteral("baffles"),
    QStringLiteral("bagpipe"), QStringLiteral("bailed"), QStringLiteral("bakery"), QStringLiteral("balding"), QStringLiteral("bamboo"), QStringLiteral("banjo"), QStringLiteral("baptism"), QStringLiteral("basin"),
    QStringLiteral("batch"), QStringLiteral("bawled"), QStringLiteral("bays"), QStringLiteral("because"), QStringLiteral("beer"), QStringLiteral("befit"), QStringLiteral("begun"), QStringLiteral("behind"),
    QStringLiteral("being"), QStringLiteral("below"), QStringLiteral("bemused"), QStringLiteral("benches"), QStringLiteral("berries"), QStringLiteral("bested"), QStringLiteral("betting"), QStringLiteral("bevel"),
    QStringLiteral("beware"), QStringLiteral("beyond"), QStringLiteral("bias"), QStringLiteral("bicycle"), QStringLiteral("bids"), QStringLiteral("bifocals"), QStringLiteral("biggest"), QStringLiteral("bikini"),
    QStringLiteral("bimonthly"), QStringLiteral("binocular"), QStringLiteral("biology"), QStringLiteral("biplane"), QStringLiteral("birth"), QStringLiteral("biscuit"), QStringLiteral("bite"), QStringLiteral("biweekly"),
    QStringLiteral("blender"), QStringLiteral("blip"), QStringLiteral("bluntly"), QStringLiteral("boat"), QStringLiteral("bobsled"), QStringLiteral("bodies"), QStringLiteral("bogeys"), QStringLiteral("boil"),
    QStringLiteral("boldly"), QStringLiteral("bomb"), QStringLiteral("border"), QStringLiteral("boss"), QStringLiteral("both"), QStringLiteral("bounced"), QStringLiteral("bovine"), QStringLiteral("bowling"),
    QStringLiteral("boxes"), QStringLiteral("boyfriend"), QStringLiteral("broken"), QStringLiteral("brunt"), QStringLiteral("bubble"), QStringLiteral("buckets"), QStringLiteral("budget"), QStringLiteral("buffet"),
    QStringLiteral("bugs"), QStringLiteral("building"), QStringLiteral("bulb"), QStringLiteral("bumper"), QStringLiteral("bunch"), QStringLiteral("business"), QStringLiteral("butter"), QStringLiteral("buying"),
    QStringLiteral("buzzer"), QStringLiteral("bygones"), QStringLiteral("byline"), QStringLiteral("bypass"), QStringLiteral("cabin"), QStringLiteral("cactus"), QStringLiteral("cadets"), QStringLiteral("cafe"),
    QStringLiteral("cage"), QStringLiteral("cajun"), QStringLiteral("cake"), QStringLiteral("calamity"), QStringLiteral("camp"), QStringLiteral("candy"), QStringLiteral("casket"), QStringLiteral("catch"),
    QStringLiteral("cause"), QStringLiteral("cavernous"), QStringLiteral("cease"), QStringLiteral("cedar"), QStringLiteral("ceiling"), QStringLiteral("cell"), QStringLiteral("cement"), QStringLiteral("cent"),
    QStringLiteral("certain"), QStringLiteral("chlorine"), QStringLiteral("chrome"), QStringLiteral("cider"), QStringLiteral("cigar"), QStringLiteral("cinema"), QStringLiteral("circle"), QStringLiteral("cistern"),
    QStringLiteral("citadel"), QStringLiteral("civilian"), QStringLiteral("claim"), QStringLiteral("click"), QStringLiteral("clue"), QStringLiteral("coal"), QStringLiteral("cobra"), QStringLiteral("cocoa"),
    QStringLiteral("code"), QStringLiteral("coexist"), QStringLiteral("coffee"), QStringLiteral("cogs"), QStringLiteral("cohesive"), QStringLiteral("coils"), QStringLiteral("colony"), QStringLiteral("comb"),
    QStringLiteral("cool"), QStringLiteral("copy"), QStringLiteral("corrode"), QStringLiteral("costume"), QStringLiteral("cottage"), QStringLiteral("cousin"), QStringLiteral("cowl"), QStringLiteral("criminal"),
    QStringLiteral("cube"), QStringLiteral("cucumber"), QStringLiteral("cuddled"), QStringLiteral("cuffs"), QStringLiteral("cuisine"), QStringLiteral("cunning"), QStringLiteral("cupcake"), QStringLiteral("custom"),
    QStringLiteral("cycling"), QStringLiteral("cylinder"), QStringLiteral("cynical"), QStringLiteral("dabbing"), QStringLiteral("dads"), QStringLiteral("daft"), QStringLiteral("dagger"), QStringLiteral("daily"),
    QStringLiteral("damp"), QStringLiteral("dangerous"), QStringLiteral("dapper"), QStringLiteral("darted"), QStringLiteral("dash"), QStringLiteral("dating"), QStringLiteral("dauntless"), QStringLiteral("dawn"),
    QStringLiteral("daytime"), QStringLiteral("dazed"), QStringLiteral("debut"), QStringLiteral("decay"), QStringLiteral("dedicated"), QStringLiteral("deepest"), QStringLiteral("deftly"), QStringLiteral("degrees"),
    QStringLiteral("dehydrate"), QStringLiteral("deity"), QStringLiteral("dejected"), QStringLiteral("delayed"), QStringLiteral("demonstrate"), QStringLiteral("dented"), QStringLiteral("deodorant"), QStringLiteral("depth"),
    QStringLiteral("desk"), QStringLiteral("devoid"), QStringLiteral("dewdrop"), QStringLiteral("dexterity"), QStringLiteral("dialect"), QStringLiteral("dice"), QStringLiteral("diet"), QStringLiteral("different"),
    QStringLiteral("digit"), QStringLiteral("dilute"), QStringLiteral("dime"), QStringLiteral("dinner"), QStringLiteral("diode"), QStringLiteral("diplomat"), QStringLiteral("directed"), QStringLiteral("distance"),
    QStringLiteral("ditch"), QStringLiteral("divers"), QStringLiteral("dizzy"), QStringLiteral("doctor"), QStringLiteral("dodge"), QStringLiteral("does"), QStringLiteral("dogs"), QStringLiteral("doing"),
    QStringLiteral("dolphin"), QStringLiteral("domestic"), QStringLiteral("donuts"), QStringLiteral("doorway"), QStringLiteral("dormant"), QStringLiteral("dosage"), QStringLiteral("dotted"), QStringLiteral("double"),
    QStringLiteral("dove"), QStringLiteral("down"), QStringLiteral("dozen"), QStringLiteral("dreams"), QStringLiteral("drinks"), QStringLiteral("drowning"), QStringLiteral("drunk"), QStringLiteral("drying"),
    QStringLiteral("dual"), QStringLiteral("dubbed"), QStringLiteral("duckling"), QStringLiteral("dude"), QStringLiteral("duets"), QStringLiteral("duke"), QStringLiteral("dullness"), QStringLiteral("dummy"),
    QStringLiteral("dunes"), QStringLiteral("duplex"), QStringLiteral("duration"), QStringLiteral("dusted"), QStringLiteral("duties"), QStringLiteral("dwarf"), QStringLiteral("dwelt"), QStringLiteral("dwindling"),
    QStringLiteral("dying"), QStringLiteral("dynamite"), QStringLiteral("dyslexic"), QStringLiteral("each"), QStringLiteral("eagle"), QStringLiteral("earth"), QStringLiteral("easy"), QStringLiteral("eating"),
    QStringLiteral("eavesdrop"), QStringLiteral("eccentric"), QStringLiteral("echo"), QStringLiteral("eclipse"), QStringLiteral("economics"), QStringLiteral("ecstatic"), QStringLiteral("eden"), QStringLiteral("edgy"),
    QStringLiteral("edited"), QStringLiteral("educated"), QStringLiteral("eels"), QStringLiteral("efficient"), QStringLiteral("eggs"), QStringLiteral("egotistic"), QStringLiteral("eight"), QStringLiteral("either"),
    QStringLiteral("eject"), QStringLiteral("elapse"), QStringLiteral("elbow"), QStringLiteral("eldest"), QStringLiteral("eleven"), QStringLiteral("elite"), QStringLiteral("elope"), QStringLiteral("else"),
    QStringLiteral("eluded"), QStringLiteral("emails"), QStringLiteral("ember"), QStringLiteral("emerge"), QStringLiteral("emit"), QStringLiteral("emotion"), QStringLiteral("empty"), QStringLiteral("emulate"),
    QStringLiteral("energy"), QStringLiteral("enforce"), QStringLiteral("enhanced"), QStringLiteral("enigma"), QStringLiteral("enjoy"), QStringLiteral("enlist"), QStringLiteral("enmity"), QStringLiteral("enough"),
    QStringLiteral("enraged"), QStringLiteral("ensign"), QStringLiteral("entrance"), QStringLiteral("envy"), QStringLiteral("epoxy"), QStringLiteral("equip"), QStringLiteral("erase"), QStringLiteral("erected"),
    QStringLiteral("erosion"), QStringLiteral("error"), QStringLiteral("eskimos"), QStringLiteral("espionage"), QStringLiteral("essential"), QStringLiteral("estate"), QStringLiteral("etched"), QStringLiteral("eternal"),
    QStringLiteral("ethics"), QStringLiteral("etiquette"), QStringLiteral("evaluate"), QStringLiteral("evenings"), QStringLiteral("evicted"), QStringLiteral("evolved"), QStringLiteral("examine"), QStringLiteral("excess"),
    QStringLiteral("exhale"), QStringLiteral("exit"), QStringLiteral("exotic"), QStringLiteral("exquisite"), QStringLiteral("extra"), QStringLiteral("exult"), QStringLiteral("fabrics"), QStringLiteral("factual"),
    QStringLiteral("fading"), QStringLiteral("fainted"), QStringLiteral("faked"), QStringLiteral("fall"), QStringLiteral("family"), QStringLiteral("fancy"), QStringLiteral("farming"), QStringLiteral("fatal"),
    QStringLiteral("faulty"), QStringLiteral("fawns"), QStringLiteral("faxed"), QStringLiteral("fazed"), QStringLiteral("feast"), QStringLiteral("february"), QStringLiteral("federal"), QStringLiteral("feel"),
    QStringLiteral("feline"), QStringLiteral("females"), QStringLiteral("fences"), QStringLiteral("ferry"), QStringLiteral("festival"), QStringLiteral("fetches"), QStringLiteral("fever"), QStringLiteral("fewest"),
    QStringLiteral("fiat"), QStringLiteral("fibula"), QStringLiteral("fictional"), QStringLiteral("fidget"), QStringLiteral("fierce"), QStringLiteral("fifteen"), QStringLiteral("fight"), QStringLiteral("films"),
    QStringLiteral("firm"), QStringLiteral("fishing"), QStringLiteral("fitting"), QStringLiteral("five"), QStringLiteral("fixate"), QStringLiteral("fizzle"), QStringLiteral("fleet"), QStringLiteral("flippant"),
    QStringLiteral("flying"), QStringLiteral("foamy"), QStringLiteral("focus"), QStringLiteral("foes"), QStringLiteral("foggy"), QStringLiteral("foiled"), QStringLiteral("folding"), QStringLiteral("fonts"),
    QStringLiteral("foolish"), QStringLiteral("fossil"), QStringLiteral("fountain"), QStringLiteral("fowls"), QStringLiteral("foxes"), QStringLiteral("foyer"), QStringLiteral("framed"), QStringLiteral("friendly"),
    QStringLiteral("frown"), QStringLiteral("fruit"), QStringLiteral("frying"), QStringLiteral("fudge"), QStringLiteral("fuel"), QStringLiteral("fugitive"), QStringLiteral("fully"), QStringLiteral("fuming"),
    QStringLiteral("fungal"), QStringLiteral("furnished"), QStringLiteral("fuselage"), QStringLiteral("future"), QStringLiteral("fuzzy"), QStringLiteral("gables"), QStringLiteral("gadget"), QStringLiteral("gags"),
    QStringLiteral("gained"), QStringLiteral("galaxy"), QStringLiteral("gambit"), QStringLiteral("gang"), QStringLiteral("gasp"), QStringLiteral("gather"), QStringLiteral("gauze"), QStringLiteral("gave"),
    QStringLiteral("gawk"), QStringLiteral("gaze"), QStringLiteral("gearbox"), QStringLiteral("gecko"), QStringLiteral("geek"), QStringLiteral("gels"), QStringLiteral("gemstone"), QStringLiteral("general"),
    QStringLiteral("geometry"), QStringLiteral("germs"), QStringLiteral("gesture"), QStringLiteral("getting"), QStringLiteral("geyser"), QStringLiteral("ghetto"), QStringLiteral("ghost"), QStringLiteral("giant"),
    QStringLiteral("giddy"), QStringLiteral("gifts"), QStringLiteral("gigantic"), QStringLiteral("gills"), QStringLiteral("gimmick"), QStringLiteral("ginger"), QStringLiteral("girth"), QStringLiteral("giving"),
    QStringLiteral("glass"), QStringLiteral("gleeful"), QStringLiteral("glide"), QStringLiteral("gnaw"), QStringLiteral("gnome"), QStringLiteral("goat"), QStringLiteral("goblet"), QStringLiteral("godfather"),
    QStringLiteral("goes"), QStringLiteral("goggles"), QStringLiteral("going"), QStringLiteral("goldfish"), QStringLiteral("gone"), QStringLiteral("goodbye"), QStringLiteral("gopher"), QStringLiteral("gorilla"),
    QStringLiteral("gossip"), QStringLiteral("gotten"), QStringLiteral("gourmet"), QStringLiteral("governing"), QStringLiteral("gown"), QStringLiteral("greater"), QStringLiteral("grunt"), QStringLiteral("guarded"),
    QStringLiteral("guest"), QStringLiteral("guide"), QStringLiteral("gulp"), QStringLiteral("gumball"), QStringLiteral("guru"), QStringLiteral("gusts"), QStringLiteral("gutter"), QStringLiteral("guys"),
    QStringLiteral("gymnast"), QStringLiteral("gypsy"), QStringLiteral("gyrate"), QStringLiteral("habitat"), QStringLiteral("hacksaw"), QStringLiteral("haggled"), QStringLiteral("hairy"), QStringLiteral("hamburger"),
    QStringLiteral("happens"), QStringLiteral("hashing"), QStringLiteral("hatchet"), QStringLiteral("haunted"), QStringLiteral("having"), QStringLiteral("hawk"), QStringLiteral("haystack"), QStringLiteral("hazard"),
    QStringLiteral("hectare"), QStringLiteral("hedgehog"), QStringLiteral("heels"), QStringLiteral("hefty"), QStringLiteral("height"), QStringLiteral("hemlock"), QStringLiteral("hence"), QStringLiteral("heron"),
    QStringLiteral("hesitate"), QStringLiteral("hexagon"), QStringLiteral("hickory"), QStringLiteral("hiding"), QStringLiteral("highway"), QStringLiteral("hijack"), QStringLiteral("hiker"), QStringLiteral("hills"),
    QStringLiteral("himself"), QStringLiteral("hinder"), QStringLiteral("hippo"), QStringLiteral("hire"), QStringLiteral("history"), QStringLiteral("hitched"), QStringLiteral("hive"), QStringLiteral("hoax"),
    QStringLiteral("hobby"), QStringLiteral("hockey"), QStringLiteral("hoisting"), QStringLiteral("hold"), QStringLiteral("honked"), QStringLiteral("hookup"), QStringLiteral("hope"), QStringLiteral("hornet"),
    QStringLiteral("hospital"), QStringLiteral("hotel"), QStringLiteral("hounded"), QStringLiteral("hover"), QStringLiteral("howls"), QStringLiteral("hubcaps"), QStringLiteral("huddle"), QStringLiteral("huge"),
    QStringLiteral("hull"), QStringLiteral("humid"), QStringLiteral("hunter"), QStringLiteral("hurried"), QStringLiteral("husband"), QStringLiteral("huts"), QStringLiteral("hybrid"), QStringLiteral("hydrogen"),
    QStringLiteral("hyper"), QStringLiteral("iceberg"), QStringLiteral("icing"), QStringLiteral("icon"), QStringLiteral("identity"), QStringLiteral("idiom"), QStringLiteral("idled"), QStringLiteral("idols"),
    QStringLiteral("igloo"), QStringLiteral("ignore"), QStringLiteral("iguana"), QStringLiteral("illness"), QStringLiteral("imagine"), QStringLiteral("imbalance"), QStringLiteral("imitate"), QStringLiteral("impel"),
    QStringLiteral("inactive"), QStringLiteral("inbound"), QStringLiteral("incur"), QStringLiteral("industrial"), QStringLiteral("inexact"), QStringLiteral("inflamed"), QStringLiteral("ingested"), QStringLiteral("initiate"),
    QStringLiteral("injury"), QStringLiteral("inkling"), QStringLiteral("inline"), QStringLiteral("inmate"), QStringLiteral("innocent"), QStringLiteral("inorganic"), QStringLiteral("input"), QStringLiteral("inquest"),
    QStringLiteral("inroads"), QStringLiteral("insult"), QStringLiteral("intended"), QStringLiteral("inundate"), QStringLiteral("invoke"), QStringLiteral("inwardly"), QStringLiteral("ionic"), QStringLiteral("irate"),
    QStringLiteral("iris"), QStringLiteral("irony"), QStringLiteral("irritate"), QStringLiteral("island"), QStringLiteral("isolated"), QStringLiteral("issued"), QStringLiteral("italics"), QStringLiteral("itches"),
    QStringLiteral("items"), QStringLiteral("itinerary"), QStringLiteral("itself"), QStringLiteral("ivory"), QStringLiteral("jabbed"), QStringLiteral("jackets"), QStringLiteral("jaded"), QStringLiteral("jagged"),
    QStringLiteral("jailed"), QStringLiteral("jamming"), QStringLiteral("january"), QStringLiteral("jargon"), QStringLiteral("jaunt"), QStringLiteral("javelin"), QStringLiteral("jaws"), QStringLiteral("jazz"),
    QStringLiteral("jeans"), QStringLiteral("jeers"), QStringLiteral("jellyfish"), QStringLiteral("jeopardy"), QStringLiteral("jerseys"), QStringLiteral("jester"), QStringLiteral("jetting"), QStringLiteral("jewels"),
    QStringLiteral("jigsaw"), QStringLiteral("jingle"), QStringLiteral("jittery"), QStringLiteral("jive"), QStringLiteral("jobs"), QStringLiteral("jockey"), QStringLiteral("jogger"), QStringLiteral("joining"),
    QStringLiteral("joking"), QStringLiteral("jolted"), QStringLiteral("jostle"), QStringLiteral("journal"), QStringLiteral("joyous"), QStringLiteral("jubilee"), QStringLiteral("judge"), QStringLiteral("juggled"),
    QStringLiteral("juicy"), QStringLiteral("jukebox"), QStringLiteral("july"), QStringLiteral("jump"), QStringLiteral("junk"), QStringLiteral("jury"), QStringLiteral("justice"), QStringLiteral("juvenile"),
    QStringLiteral("kangaroo"), QStringLiteral("karate"), QStringLiteral("keep"), QStringLiteral("kennel"), QStringLiteral("kept"), QStringLiteral("kernels"), QStringLiteral("kettle"), QStringLiteral("keyboard"),
    QStringLiteral("kickoff"), QStringLiteral("kidneys"), QStringLiteral("king"), QStringLiteral("kiosk"), QStringLiteral("kisses"), QStringLiteral("kitchens"), QStringLiteral("kiwi"), QStringLiteral("knapsack"),
    QStringLiteral("knee"), QStringLiteral("knife"), QStringLiteral("knowledge"), QStringLiteral("knuckle"), QStringLiteral("koala"), QStringLiteral("laboratory"), QStringLiteral("ladder"), QStringLiteral("lagoon"),
    QStringLiteral("lair"), QStringLiteral("lakes"), QStringLiteral("lamb"), QStringLiteral("language"), QStringLiteral("laptop"), QStringLiteral("large"), QStringLiteral("last"), QStringLiteral("later"),
    QStringLiteral("launching"), QStringLiteral("lava"), QStringLiteral("lawsuit"), QStringLiteral("layout"), QStringLiteral("lazy"), QStringLiteral("lectures"), QStringLiteral("ledge"), QStringLiteral("leech"),
    QStringLiteral("left"), QStringLiteral("legion"), QStringLiteral("leisure"), QStringLiteral("lemon"), QStringLiteral("lending"), QStringLiteral("leopard"), QStringLiteral("lesson"), QStringLiteral("lettuce"),
    QStringLiteral("lexicon"), QStringLiteral("liar"), QStringLiteral("library"), QStringLiteral("licks"), QStringLiteral("lids"), QStringLiteral("lied"), QStringLiteral("lifestyle"), QStringLiteral("light"),
    QStringLiteral("likewise"), QStringLiteral("lilac"), QStringLiteral("limits"), QStringLiteral("linen"), QStringLiteral("lion"), QStringLiteral("lipstick"), QStringLiteral("liquid"), QStringLiteral("listen"),
    QStringLiteral("lively"), QStringLiteral("loaded"), QStringLiteral("lobster"), QStringLiteral("locker"), QStringLiteral("lodge"), QStringLiteral("lofty"), QStringLiteral("logic"), QStringLiteral("loincloth"),
    QStringLiteral("long"), QStringLiteral("looking"), QStringLiteral("lopped"), QStringLiteral("lordship"), QStringLiteral("losing"), QStringLiteral("lottery"), QStringLiteral("loudly"), QStringLiteral("love"),
    QStringLiteral("lower"), QStringLiteral("loyal"), QStringLiteral("lucky"), QStringLiteral("luggage"), QStringLiteral("lukewarm"), QStringLiteral("lullaby"), QStringLiteral("lumber"), QStringLiteral("lunar"),
    QStringLiteral("lurk"), QStringLiteral("lush"), QStringLiteral("luxury"), QStringLiteral("lymph"), QStringLiteral("lynx"), QStringLiteral("lyrics"), QStringLiteral("macro"), QStringLiteral("madness"),
    QStringLiteral("magically"), QStringLiteral("mailed"), QStringLiteral("major"), QStringLiteral("makeup"), QStringLiteral("malady"), QStringLiteral("mammal"), QStringLiteral("maps"), QStringLiteral("masterful"),
    QStringLiteral("match"), QStringLiteral("maul"), QStringLiteral("maverick"), QStringLiteral("maximum"), QStringLiteral("mayor"), QStringLiteral("maze"), QStringLiteral("meant"), QStringLiteral("mechanic"),
    QStringLiteral("medicate"), QStringLiteral("meeting"), QStringLiteral("megabyte"), QStringLiteral("melting"), QStringLiteral("memoir"), QStringLiteral("menu"), QStringLiteral("merger"), QStringLiteral("mesh"),
    QStringLiteral("metro"), QStringLiteral("mews"), QStringLiteral("mice"), QStringLiteral("midst"), QStringLiteral("mighty"), QStringLiteral("mime"), QStringLiteral("mirror"), QStringLiteral("misery"),
    QStringLiteral("mittens"), QStringLiteral("mixture"), QStringLiteral("moat"), QStringLiteral("mobile"), QStringLiteral("mocked"), QStringLiteral("mohawk"), QStringLiteral("moisture"), QStringLiteral("molten"),
    QStringLiteral("moment"), QStringLiteral("money"), QStringLiteral("moon"), QStringLiteral("mops"), QStringLiteral("morsel"), QStringLiteral("mostly"), QStringLiteral("motherly"), QStringLiteral("mouth"),
    QStringLiteral("movement"), QStringLiteral("mowing"), QStringLiteral("much"), QStringLiteral("muddy"), QStringLiteral("muffin"), QStringLiteral("mugged"), QStringLiteral("mullet"), QStringLiteral("mumble"),
    QStringLiteral("mundane"), QStringLiteral("muppet"), QStringLiteral("mural"), QStringLiteral("musical"), QStringLiteral("muzzle"), QStringLiteral("myriad"), QStringLiteral("mystery"), QStringLiteral("myth"),
    QStringLiteral("nabbing"), QStringLiteral("nagged"), QStringLiteral("nail"), QStringLiteral("names"), QStringLiteral("nanny"), QStringLiteral("napkin"), QStringLiteral("narrate"), QStringLiteral("nasty"),
    QStringLiteral("natural"), QStringLiteral("nautical"), QStringLiteral("navy"), QStringLiteral("nearby"), QStringLiteral("necklace"), QStringLiteral("needed"), QStringLiteral("negative"), QStringLiteral("neither"),
    QStringLiteral("neon"), QStringLiteral("nephew"), QStringLiteral("nerves"), QStringLiteral("nestle"), QStringLiteral("network"), QStringLiteral("neutral"), QStringLiteral("never"), QStringLiteral("newt"),
    QStringLiteral("nexus"), QStringLiteral("nibs"), QStringLiteral("niche"), QStringLiteral("niece"), QStringLiteral("nifty"), QStringLiteral("nightly"), QStringLiteral("nimbly"), QStringLiteral("nineteen"),
    QStringLiteral("nirvana"), QStringLiteral("nitrogen"), QStringLiteral("nobody"), QStringLiteral("nocturnal"), QStringLiteral("nodes"), QStringLiteral("noises"), QStringLiteral("nomad"), QStringLiteral("noodles"),
    QStringLiteral("northern"), QStringLiteral("nostril"), QStringLiteral("noted"), QStringLiteral("nouns"), QStringLiteral("novelty"), QStringLiteral("nowhere"), QStringLiteral("nozzle"), QStringLiteral("nuance"),
    QStringLiteral("nucleus"), QStringLiteral("nudged"), QStringLiteral("nugget"), QStringLiteral("nuisance"), QStringLiteral("null"), QStringLiteral("number"), QStringLiteral("nuns"), QStringLiteral("nurse"),
    QStringLiteral("nutshell"), QStringLiteral("nylon"), QStringLiteral("oaks"), QStringLiteral("oars"), QStringLiteral("oasis"), QStringLiteral("oatmeal"), QStringLiteral("obedient"), QStringLiteral("object"),
    QStringLiteral("obliged"), QStringLiteral("obnoxious"), QStringLiteral("observant"), QStringLiteral("obtains"), QStringLiteral("obvious"), QStringLiteral("occur"), QStringLiteral("ocean"), QStringLiteral("october"),
    QStringLiteral("odds"), QStringLiteral("odometer"), QStringLiteral("offend"), QStringLiteral("often"), QStringLiteral("oilfield"), QStringLiteral("ointment"), QStringLiteral("okay"), QStringLiteral("older"),
    QStringLiteral("olive"), QStringLiteral("olympics"), QStringLiteral("omega"), QStringLiteral("omission"), QStringLiteral("omnibus"), QStringLiteral("onboard"), QStringLiteral("oncoming"), QStringLiteral("oneself"),
    QStringLiteral("ongoing"), QStringLiteral("onion"), QStringLiteral("online"), QStringLiteral("onslaught"), QStringLiteral("onto"), QStringLiteral("onward"), QStringLiteral("oozed"), QStringLiteral("opacity"),
    QStringLiteral("opened"), QStringLiteral("opposite"), QStringLiteral("optical"), QStringLiteral("opus"), QStringLiteral("orange"), QStringLiteral("orbit"), QStringLiteral("orchid"), QStringLiteral("orders"),
    QStringLiteral("organs"), QStringLiteral("origin"), QStringLiteral("ornament"), QStringLiteral("orphans"), QStringLiteral("oscar"), QStringLiteral("ostrich"), QStringLiteral("otherwise"), QStringLiteral("otter"),
    QStringLiteral("ouch"), QStringLiteral("ought"), QStringLiteral("ounce"), QStringLiteral("ourselves"), QStringLiteral("oust"), QStringLiteral("outbreak"), QStringLiteral("oval"), QStringLiteral("oven"),
    QStringLiteral("owed"), QStringLiteral("owls"), QStringLiteral("owner"), QStringLiteral("oxidant"), QStringLiteral("oxygen"), QStringLiteral("oyster"), QStringLiteral("ozone"), QStringLiteral("pact"),
    QStringLiteral("paddles"), QStringLiteral("pager"), QStringLiteral("pairing"), QStringLiteral("palace"), QStringLiteral("pamphlet"), QStringLiteral("pancakes"), QStringLiteral("paper"), QStringLiteral("paradise"),
    QStringLiteral("pastry"), QStringLiteral("patio"), QStringLiteral("pause"), QStringLiteral("pavements"), QStringLiteral("pawnshop"), QStringLiteral("payment"), QStringLiteral("peaches"), QStringLiteral("pebbles"),
    QStringLiteral("peculiar"), QStringLiteral("pedantic"), QStringLiteral("peeled"), QStringLiteral("pegs"), QStringLiteral("pelican"), QStringLiteral("pencil"), QStringLiteral("people"), QStringLiteral("pepper"),
    QStringLiteral("perfect"), QStringLiteral("pests"), QStringLiteral("petals"), QStringLiteral("phase"), QStringLiteral("pheasants"), QStringLiteral("phone"), QStringLiteral("phrases"), QStringLiteral("physics"),
    QStringLiteral("piano"), QStringLiteral("picked"), QStringLiteral("pierce"), QStringLiteral("pigment"), QStringLiteral("piloted"), QStringLiteral("pimple"), QStringLiteral("pinched"), QStringLiteral("pioneer"),
    QStringLiteral("pipeline"), QStringLiteral("pirate"), QStringLiteral("pistons"), QStringLiteral("pitched"), QStringLiteral("pivot"), QStringLiteral("pixels"), QStringLiteral("pizza"), QStringLiteral("playful"),
    QStringLiteral("pledge"), QStringLiteral("pliers"), QStringLiteral("plotting"), QStringLiteral("plus"), QStringLiteral("plywood"), QStringLiteral("poaching"), QStringLiteral("pockets"), QStringLiteral("podcast"),
    QStringLiteral("poetry"), QStringLiteral("point"), QStringLiteral("poker"), QStringLiteral("polar"), QStringLiteral("ponies"), QStringLiteral("pool"), QStringLiteral("popular"), QStringLiteral("portents"),
    QStringLiteral("possible"), QStringLiteral("potato"), QStringLiteral("pouch"), QStringLiteral("poverty"), QStringLiteral("powder"), QStringLiteral("pram"), QStringLiteral("present"), QStringLiteral("pride"),
    QStringLiteral("problems"), QStringLiteral("pruned"), QStringLiteral("prying"), QStringLiteral("psychic"), QStringLiteral("public"), QStringLiteral("puck"), QStringLiteral("puddle"), QStringLiteral("puffin"),
    QStringLiteral("pulp"), QStringLiteral("pumpkins"), QStringLiteral("punch"), QStringLiteral("puppy"), QStringLiteral("purged"), QStringLiteral("push"), QStringLiteral("putty"), QStringLiteral("puzzled"),
    QStringLiteral("pylons"), QStringLiteral("pyramid"), QStringLiteral("python"), QStringLiteral("queen"), QStringLiteral("quick"), QStringLiteral("quote"), QStringLiteral("rabbits"), QStringLiteral("racetrack"),
    QStringLiteral("radar"), QStringLiteral("rafts"), QStringLiteral("rage"), QStringLiteral("railway"), QStringLiteral("raking"), QStringLiteral("rally"), QStringLiteral("ramped"), QStringLiteral("randomly"),
    QStringLiteral("rapid"), QStringLiteral("rarest"), QStringLiteral("rash"), QStringLiteral("rated"), QStringLiteral("ravine"), QStringLiteral("rays"), QStringLiteral("razor"), QStringLiteral("react"),
    QStringLiteral("rebel"), QStringLiteral("recipe"), QStringLiteral("reduce"), QStringLiteral("reef"), QStringLiteral("refer"), QStringLiteral("regular"), QStringLiteral("reheat"), QStringLiteral("reinvest"),
    QStringLiteral("rejoices"), QStringLiteral("rekindle"), QStringLiteral("relic"), QStringLiteral("remedy"), QStringLiteral("renting"), QStringLiteral("reorder"), QStringLiteral("repent"), QStringLiteral("request"),
    QStringLiteral("reruns"), QStringLiteral("rest"), QStringLiteral("return"), QStringLiteral("reunion"), QStringLiteral("revamp"), QStringLiteral("rewind"), QStringLiteral("rhino"), QStringLiteral("rhythm"),
    QStringLiteral("ribbon"), QStringLiteral("richly"), QStringLiteral("ridges"), QStringLiteral("rift"), QStringLiteral("rigid"), QStringLiteral("rims"), QStringLiteral("ringing"), QStringLiteral("riots"),
    QStringLiteral("ripped"), QStringLiteral("rising"), QStringLiteral("ritual"), QStringLiteral("river"), QStringLiteral("roared"), QStringLiteral("robot"), QStringLiteral("rockets"), QStringLiteral("rodent"),
    QStringLiteral("rogue"), QStringLiteral("roles"), QStringLiteral("romance"), QStringLiteral("roomy"), QStringLiteral("roped"), QStringLiteral("roster"), QStringLiteral("rotate"), QStringLiteral("rounded"),
    QStringLiteral("rover"), QStringLiteral("rowboat"), QStringLiteral("royal"), QStringLiteral("ruby"), QStringLiteral("rudely"), QStringLiteral("ruffled"), QStringLiteral("rugged"), QStringLiteral("ruined"),
    QStringLiteral("ruling"), QStringLiteral("rumble"), QStringLiteral("runway"), QStringLiteral("rural"), QStringLiteral("rustled"), QStringLiteral("ruthless"), QStringLiteral("sabotage"), QStringLiteral("sack"),
    QStringLiteral("sadness"), QStringLiteral("safety"), QStringLiteral("saga"), QStringLiteral("sailor"), QStringLiteral("sake"), QStringLiteral("salads"), QStringLiteral("sample"), QStringLiteral("sanity"),
    QStringLiteral("sapling"), QStringLiteral("sarcasm"), QStringLiteral("sash"), QStringLiteral("satin"), QStringLiteral("saucepan"), QStringLiteral("saved"), QStringLiteral("sawmill"), QStringLiteral("saxophone"),
    QStringLiteral("sayings"), QStringLiteral("scamper"), QStringLiteral("scenic"), QStringLiteral("school"), QStringLiteral("science"), QStringLiteral("scoop"), QStringLiteral("scrub"), QStringLiteral("scuba"),
    QStringLiteral("seasons"), QStringLiteral("second"), QStringLiteral("sedan"), QStringLiteral("seeded"), QStringLiteral("segments"), QStringLiteral("seismic"), QStringLiteral("selfish"), QStringLiteral("semifinal"),
    QStringLiteral("sensible"), QStringLiteral("september"), QStringLiteral("sequence"), QStringLiteral("serving"), QStringLiteral("session"), QStringLiteral("setup"), QStringLiteral("seventh"), QStringLiteral("sewage"),
    QStringLiteral("shackles"), QStringLiteral("shelter"), QStringLiteral("shipped"), QStringLiteral("shocking"), QStringLiteral("shrugged"), QStringLiteral("shuffled"), QStringLiteral("shyness"), QStringLiteral("siblings"),
    QStringLiteral("sickness"), QStringLiteral("sidekick"), QStringLiteral("sieve"), QStringLiteral("sifting"), QStringLiteral("sighting"), QStringLiteral("silk"), QStringLiteral("simplest"), QStringLiteral("sincerely"),
    QStringLiteral("sipped"), QStringLiteral("siren"), QStringLiteral("situated"), QStringLiteral("sixteen"), QStringLiteral("sizes"), QStringLiteral("skater"), QStringLiteral("skew"), QStringLiteral("skirting"),
    QStringLiteral("skulls"), QStringLiteral("skydive"), QStringLiteral("slackens"), QStringLiteral("sleepless"), QStringLiteral("slid"), QStringLiteral("slower"), QStringLiteral("slug"), QStringLiteral("smash"),
    QStringLiteral("smelting"), QStringLiteral("smidgen"), QStringLiteral("smog"), QStringLiteral("smuggled"), QStringLiteral("snake"), QStringLiteral("sneeze"), QStringLiteral("sniff"), QStringLiteral("snout"),
    QStringLiteral("snug"), QStringLiteral("soapy"), QStringLiteral("sober"), QStringLiteral("soccer"), QStringLiteral("soda"), QStringLiteral("software"), QStringLiteral("soggy"), QStringLiteral("soil"),
    QStringLiteral("solved"), QStringLiteral("somewhere"), QStringLiteral("sonic"), QStringLiteral("soothe"), QStringLiteral("soprano"), QStringLiteral("sorry"), QStringLiteral("southern"), QStringLiteral("sovereign"),
    QStringLiteral("sowed"), QStringLiteral("soya"), QStringLiteral("space"), QStringLiteral("speedy"), QStringLiteral("sphere"), QStringLiteral("spiders"), QStringLiteral("splendid"), QStringLiteral("spout"),
    QStringLiteral("sprig"), QStringLiteral("spud"), QStringLiteral("spying"), QStringLiteral("square"), QStringLiteral("stacking"), QStringLiteral("stellar"), QStringLiteral("stick"), QStringLiteral("stockpile"),
    QStringLiteral("strained"), QStringLiteral("stunning"), QStringLiteral("stylishly"), QStringLiteral("subtly"), QStringLiteral("succeed"), QStringLiteral("suddenly"), QStringLiteral("suede"), QStringLiteral("suffice"),
    QStringLiteral("sugar"), QStringLiteral("suitcase"), QStringLiteral("sulking"), QStringLiteral("summon"), QStringLiteral("sunken"), QStringLiteral("superior"), QStringLiteral("surfer"), QStringLiteral("sushi"),
    QStringLiteral("suture"), QStringLiteral("swagger"), QStringLiteral("swept"), QStringLiteral("swiftly"), QStringLiteral("sword"), QStringLiteral("swung"), QStringLiteral("syllabus"), QStringLiteral("symptoms"),
    QStringLiteral("syndrome"), QStringLiteral("syringe"), QStringLiteral("system"), QStringLiteral("taboo"), QStringLiteral("tacit"), QStringLiteral("tadpoles"), QStringLiteral("tagged"), QStringLiteral("tail"),
    QStringLiteral("taken"), QStringLiteral("talent"), QStringLiteral("tamper"), QStringLiteral("tanks"), QStringLiteral("tapestry"), QStringLiteral("tarnished"), QStringLiteral("tasked"), QStringLiteral("tattoo"),
    QStringLiteral("taunts"), QStringLiteral("tavern"), QStringLiteral("tawny"), QStringLiteral("taxi"), QStringLiteral("teardrop"), QStringLiteral("technical"), QStringLiteral("tedious"), QStringLiteral("teeming"),
    QStringLiteral("tell"), QStringLiteral("template"), QStringLiteral("tender"), QStringLiteral("tepid"), QStringLiteral("tequila"), QStringLiteral("terminal"), QStringLiteral("testing"), QStringLiteral("tether"),
    QStringLiteral("textbook"), QStringLiteral("thaw"), QStringLiteral("theatrics"), QStringLiteral("thirsty"), QStringLiteral("thorn"), QStringLiteral("threaten"), QStringLiteral("thumbs"), QStringLiteral("thwart"),
    QStringLiteral("ticket"), QStringLiteral("tidy"), QStringLiteral("tiers"), QStringLiteral("tiger"), QStringLiteral("tilt"), QStringLiteral("timber"), QStringLiteral("tinted"), QStringLiteral("tipsy"),
    QStringLiteral("tirade"), QStringLiteral("tissue"), QStringLiteral("titans"), QStringLiteral("toaster"), QStringLiteral("tobacco"), QStringLiteral("today"), QStringLiteral("toenail"), QStringLiteral("toffee"),
    QStringLiteral("together"), QStringLiteral("toilet"), QStringLiteral("token"), QStringLiteral("tolerant"), QStringLiteral("tomorrow"), QStringLiteral("tonic"), QStringLiteral("toolbox"), QStringLiteral("topic"),
    QStringLiteral("torch"), QStringLiteral("tossed"), QStringLiteral("total"), QStringLiteral("touchy"), QStringLiteral("towel"), QStringLiteral("toxic"), QStringLiteral("toyed"), QStringLiteral("trash"),
    QStringLiteral("trendy"), QStringLiteral("tribal"), QStringLiteral("trolling"), QStringLiteral("truth"), QStringLiteral("trying"), QStringLiteral("tsunami"), QStringLiteral("tubes"), QStringLiteral("tucks"),
    QStringLiteral("tudor"), QStringLiteral("tuesday"), QStringLiteral("tufts"), QStringLiteral("tugs"), QStringLiteral("tuition"), QStringLiteral("tulips"), QStringLiteral("tumbling"), QStringLiteral("tunnel"),
    QStringLiteral("turnip"), QStringLiteral("tusks"), QStringLiteral("tutor"), QStringLiteral("tuxedo"), QStringLiteral("twang"), QStringLiteral("tweezers"), QStringLiteral("twice"), QStringLiteral("twofold"),
    QStringLiteral("tycoon"), QStringLiteral("typist"), QStringLiteral("tyrant"), QStringLiteral("ugly"), QStringLiteral("ulcers"), QStringLiteral("ultimate"), QStringLiteral("umbrella"), QStringLiteral("umpire"),
    QStringLiteral("unafraid"), QStringLiteral("unbending"), QStringLiteral("uncle"), QStringLiteral("under"), QStringLiteral("uneven"), QStringLiteral("unfit"), QStringLiteral("ungainly"), QStringLiteral("unhappy"),
    QStringLiteral("union"), QStringLiteral("unjustly"), QStringLiteral("unknown"), QStringLiteral("unlikely"), QStringLiteral("unmask"), QStringLiteral("unnoticed"), QStringLiteral("unopened"), QStringLiteral("unplugs"),
    QStringLiteral("unquoted"), QStringLiteral("unrest"), QStringLiteral("unsafe"), QStringLiteral("until"), QStringLiteral("unusual"), QStringLiteral("unveil"), QStringLiteral("unwind"), QStringLiteral("unzip"),
    QStringLiteral("upbeat"), QStringLiteral("upcoming"), QStringLiteral("update"), QStringLiteral("upgrade"), QStringLiteral("uphill"), QStringLiteral("upkeep"), QStringLiteral("upload"), QStringLiteral("upon"),
    QStringLiteral("upper"), QStringLiteral("upright"), QStringLiteral("upstairs"), QStringLiteral("uptight"), QStringLiteral("upwards"), QStringLiteral("urban"), QStringLiteral("urchins"), QStringLiteral("urgent"),
    QStringLiteral("usage"), QStringLiteral("useful"), QStringLiteral("usher"), QStringLiteral("using"), QStringLiteral("usual"), QStringLiteral("utensils"), QStringLiteral("utility"), QStringLiteral("utmost"),
    QStringLiteral("utopia"), QStringLiteral("uttered"), QStringLiteral("vacation"), QStringLiteral("vague"), QStringLiteral("vain"), QStringLiteral("value"), QStringLiteral("vampire"), QStringLiteral("vane"),
    QStringLiteral("vapidly"), QStringLiteral("vary"), QStringLiteral("vastness"), QStringLiteral("vats"), QStringLiteral("vaults"), QStringLiteral("vector"), QStringLiteral("veered"), QStringLiteral("vegan"),
    QStringLiteral("vehicle"), QStringLiteral("vein"), QStringLiteral("velvet"), QStringLiteral("venomous"), QStringLiteral("verification"), QStringLiteral("vessel"), QStringLiteral("veteran"), QStringLiteral("vexed"),
    QStringLiteral("vials"), QStringLiteral("vibrate"), QStringLiteral("victim"), QStringLiteral("video"), QStringLiteral("viewpoint"), QStringLiteral("vigilant"), QStringLiteral("viking"), QStringLiteral("village"),
    QStringLiteral("vinegar"), QStringLiteral("violin"), QStringLiteral("vipers"), QStringLiteral("virtual"), QStringLiteral("visited"), QStringLiteral("vitals"), QStringLiteral("vivid"), QStringLiteral("vixen"),
    QStringLiteral("vocal"), QStringLiteral("vogue"), QStringLiteral("voice"), QStringLiteral("volcano"), QStringLiteral("vortex"), QStringLiteral("voted"), QStringLiteral("voucher"), QStringLiteral("vowels"),
    QStringLiteral("voyage"), QStringLiteral("vulture"), QStringLiteral("wade"), QStringLiteral("waffle"), QStringLiteral("wagtail"), QStringLiteral("waist"), QStringLiteral("waking"), QStringLiteral("wallets"),
    QStringLiteral("wanted"), QStringLiteral("warped"), QStringLiteral("washing"), QStringLiteral("water"), QStringLiteral("waveform"), QStringLiteral("waxing"), QStringLiteral("wayside"), QStringLiteral("weavers"),
    QStringLiteral("website"), QStringLiteral("wedge"), QStringLiteral("weekday"), QStringLiteral("weird"), QStringLiteral("welders"), QStringLiteral("went"), QStringLiteral("wept"), QStringLiteral("were"),
    QStringLiteral("western"), QStringLiteral("wetsuit"), QStringLiteral("whale"), QStringLiteral("when"), QStringLiteral("whipped"), QStringLiteral("whole"), QStringLiteral("wickets"), QStringLiteral("width"),
    QStringLiteral("wield"), QStringLiteral("wife"), QStringLiteral("wiggle"), QStringLiteral("wildly"), QStringLiteral("winter"), QStringLiteral("wipeout"), QStringLiteral("wiring"), QStringLiteral("wise"),
    QStringLiteral("withdrawn"), QStringLiteral("wives"), QStringLiteral("wizard"), QStringLiteral("wobbly"), QStringLiteral("woes"), QStringLiteral("woken"), QStringLiteral("wolf"), QStringLiteral("womanly"),
    QStringLiteral("wonders"), QStringLiteral("woozy"), QStringLiteral("worry"), QStringLiteral("wounded"), QStringLiteral("woven"), QStringLiteral("wrap"), QStringLiteral("wrist"), QStringLiteral("wrong"),
    QStringLiteral("yacht"), QStringLiteral("yahoo"), QStringLiteral("yanks"), QStringLiteral("yard"), QStringLiteral("yawning"), QStringLiteral("yearbook"), QStringLiteral("yellow"), QStringLiteral("yesterday"),
    QStringLiteral("yeti"), QStringLiteral("yields"), QStringLiteral("yodel"), QStringLiteral("yoga"), QStringLiteral("younger"), QStringLiteral("yoyo"), QStringLiteral("zapped"), QStringLiteral("zeal"),
    QStringLiteral("zebra"), QStringLiteral("zero"), QStringLiteral("zesty"), QStringLiteral("zigzags"), QStringLiteral("zinger"), QStringLiteral("zippers"), QStringLiteral("zodiac"), QStringLiteral("zombie"),
    QStringLiteral("zones"), QStringLiteral("zoom"),
    };
    return kWords;
}

const std::vector<QString>& MnemonicWordList::english()
{
    return englishStorage();
}

int MnemonicWordList::englishSize()
{
    return static_cast<int>(englishStorage().size());
}

bool MnemonicWordList::isValidEnglishWord(const QString& word)
{
    if (word.isEmpty()) {
        return false;
    }
    const QString normalised = word.toLower();
    // Reject non-ASCII (the upstream list is 100% ASCII lowercase).
    for (QChar c : normalised) {
        if (c.unicode() > 127) {
            return false;
        }
    }
    const auto& words = englishStorage();
    return std::binary_search(words.begin(), words.end(), normalised);
}

QString MnemonicWordList::normalize(const QString& raw)
{
    // Trim leading/trailing whitespace, collapse internal runs, lowercase.
    QString out = raw.simplified().toLower();
    // simplified() already trims and collapses whitespace.
    return out;
}

QStringList MnemonicWordList::splitWords(const QString& mnemonic)
{
    // Split on any whitespace (spaces, tabs, newlines). We additionally
    // tolerate commas since some users paste "word1, word2, ..." forms.
    const QString normalised = normalize(mnemonic);
    if (normalised.isEmpty()) {
        return {};
    }
    QStringList out = normalised.split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
    return out;
}

} // namespace judecoin_wallet::core::wallet
