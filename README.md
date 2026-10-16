<h1 align="center">Kinestop </h1>

<div align="center">
  <img src="assets/logo.png" alt="Kinestop Logo" width="200">

  ---
    
  <p align="center">
  <b>(EN)</b> Kinestop is an anti-motion sickness (kinetosis) tool for the Nintendo Switch. It renders smooth floating motion cues synchronized with the console's hardware sensors, preventing nausea while playing in cars, buses, and trains.

  ---

  <b>(TR)</b> Kinestop, Nintendo Switch için araç tutmasını (kinetoz) önleyen bir hareket takip aracıdır. Konsolun donanım sensörleriyle senkronize hareket noktaları çizerek, araba, otobüs veya trende oyun oynarken oluşan mide bulantısını engeller.
  </p>

<img src="assets/kinestop.gif" alt="Kinestop Demo" width="750">

<br><br>

<a href="#en-setup-and-usage-guide" style="text-decoration:none;"><img src="https://img.shields.io/badge/Language-English-blue?style=for-the-badge" alt="English"></a> &nbsp;&nbsp; <a href="#tr-kurulum-ve-kullanım-rehberi" style="text-decoration:none;"><img src="https://img.shields.io/badge/Dil-Türkçe-red?style=for-the-badge" alt="Türkçe"></a>
</div>

---

> [!IMPORTANT]
> **This is a modified version of [Kinestop by SertAy](https://github.com/sertay1/Kinestop).** All credit for the original idea, design, motion-cue logic, color themes and guide goes to **SertAy**.
> This version is optimized to run in Ultrahand's **default 4MB** overlay memory. In line with section 2 of the [LICENSE](LICENSE), the modifications are:
> * The dots are drawn at 640×360 and scaled up to full screen by the Switch's display hardware, instead of a full 1280×720 image. This is what removes the 8MB Ultrahand requirement.
> * The settings panel and the dots are two modes of the same overlay (`kinestop.ovl`), as in Status Monitor: the new **Motion Dots** item at the top of the panel switches to the dots (the panel closes at once), and the Ultrahand key combo stops the dots and switches back to the panel. The original's **Enable Motion Dots** switch was replaced by this item, so its `enabled` config key is ignored, and **B** in the panel always leaves Kinestop for the Ultrahand menu.
> * The dots are not shown while another overlay or the Ultrahand menu is open, because the Switch can show only one overlay layer at a time. To bring them back, open Kinestop and select **Motion Dots**.
> * The settings panel no longer shows a live preview of the dots behind it; select **Motion Dots** to see your changes.
> * From the dots, only the Ultrahand key combo opens the settings; the original's left-edge touch swipe and extra button combos were removed.
> * Kinestop adds a **Motion Dots** launch mode to its entry in Ultrahand's `overlays.ini` (named in the panel's language), so Ultrahand can start the dots directly (and give them their own key combo).
> * New options: **Console Motion Sensor** (lets the dots move at the HOME menu and in homebrew such as RetroArch) and **Show in Screenshots/Videos**. The unused config keys `draw_center_reticle` / `draw_roll_indicators` are ignored.
> * A small patch to the bundled libultrahand/libtesla (marked `Kinestop patch` in the source) for the full-screen scaled layer and the motion sensor.
>
> **Bu, [SertAy'ın Kinestop](https://github.com/sertay1/Kinestop) projesinin değiştirilmiş bir sürümüdür.** Orijinal fikir, tasarım, hareket algoritması, renk temaları ve rehberin tüm hakları **SertAy**'a aittir. Bu sürüm, UltraHand'in **varsayılan 4MB** overlay belleğiyle çalışacak şekilde optimize edilmiştir: noktalar 640×360 çözünürlükte çizilip Switch'in ekran donanımıyla tam ekrana büyütülür; ayar paneli ve noktalar, Status Monitor'deki gibi aynı overlay'in iki modudur: panelin en üstündeki yeni **Hareket Noktaları** seçeneği noktalara geçer (panel hemen kapanır), UltraHand kısayolu noktaları durdurup panele döner; orijinaldeki **Hareket Noktalarını Aç** anahtarının yerini bu seçenek almıştır, bu yüzden `enabled` ayar anahtarı yok sayılır ve paneldeki **B** her zaman Kinestop'tan çıkıp UltraHand menüsüne döner; Switch aynı anda yalnızca bir overlay katmanı gösterebildiği için başka bir overlay veya UltraHand menüsü açıkken noktalar görünmez (geri getirmek için Kinestop'u açıp **Hareket Noktaları**'nı seçin); ayar paneli artık arkasında noktaların canlı önizlemesini göstermez (değişiklikleri görmek için **Hareket Noktaları**'nı seçin); noktalardan ayarlara yalnızca UltraHand kısayoluyla dönülür, orijinaldeki sol kenardan kaydırma ve ek tuş kombinasyonları kaldırılmıştır; Kinestop, UltraHand'in `overlays.ini` dosyasındaki kendi kaydına noktaları doğrudan başlatan bir başlatma modu (panelin dilinde **Hareket Noktaları** / **Motion Dots**) ekler; **Konsol Hareket Sensörü** (noktalar Ana Menüde ve RetroArch gibi homebrew'larda da hareket eder) ve **Görüntü ve Kayıtlarda Göster** seçenekleri eklenmiş, kullanılmayan `draw_center_reticle` / `draw_roll_indicators` ayar anahtarları yok sayılmaktadır; birlikte gelen libultrahand/libtesla kütüphanesine küçük bir yama yapılmıştır (kaynakta `Kinestop patch` ile işaretli). Değişiklikler [LICENSE](LICENSE) dosyasının 2. maddesi gereği burada listelenmiştir.

> [!WARNING]
> **Status:** version **2.0.0-test.6** is a **test build**. The earlier 2.0.0 test builds drew the dots from a separate sysmodule; console tests showed that the Switch never displays that layer together with an overlay menu, so that design was dropped. Since 2.0.0-test.5 Kinestop is a single overlay again, like the original, but fits in the default 4MB; test.5 ran on a console, and test.6 changes how the dots are started and left (see **Test build (2.0.0-test.6)** below for what to try). The [original release](https://github.com/sertay1/Kinestop/releases) is the stable version.
>
> **Upgrading from an earlier 2.0.0 test build:** delete the folder `SD:/atmosphere/contents/4200000000001042/` (the old sysmodule, including its `flags/boot2.flag`) and, if you like, `SD:/config/kinestop/sysmodule.log`, then reboot. Your `config.ini` is kept; the old test-build `debug_flags` setting in it is ignored.
>
> **Durum:** **2.0.0-test.6** bir **deneme sürümüdür**. Önceki 2.0.0 deneme sürümleri noktaları ayrı bir sistem modülüyle (sysmodule) çiziyordu; konsol testleri, Switch'in bu katmanı bir overlay menüsüyle hiçbir zaman aynı anda göstermediğini ortaya koydu, bu yüzden o tasarımdan vazgeçildi. 2.0.0-test.5'ten beri Kinestop orijinal gibi yine tek bir overlay'dir, ama varsayılan 4MB belleğe sığar; test.5 konsolda çalıştı, test.6 ise noktaların nasıl başlatılıp bırakıldığını değiştirir (ne denenmesi gerektiği aşağıdaki **Deneme sürümü (2.0.0-test.6)** bölümündedir). Kararlı sürüm için [orijinal sürüme](https://github.com/sertay1/Kinestop/releases) bakın.
>
> **Önceki bir 2.0.0 deneme sürümünden geçiyorsanız:** `SD:/atmosphere/contents/4200000000001042/` klasörünü (eski sistem modülü ve içindeki `flags/boot2.flag` dosyası) ve isterseniz `SD:/config/kinestop/sysmodule.log` dosyasını silin, ardından konsolu yeniden başlatın. `config.ini` dosyanız korunur; içindeki eski `debug_flags` ayarı yok sayılır.

---

## (EN) Setup and Usage Guide

### ✨ Features
* **Prevents Motion Sickness:** Floating motion cues (up to 60 FPS) sync with physical movement to eliminate nausea.
* **Universal Compatibility:** Works across all games and the Switch Home Menu.
* **Default 4MB Is Enough:** No Ultrahand memory change needed.
* **Free Floating Mode:** Dots can drift naturally beyond screen edges for an authentic horizon view.
* **Visual Customization:** 12 color themes, 6 placement patterns, dot size, and opacity options.
* **Adjustable Sensitivity:** 10 sensitivity levels (0.2x - 6.0x) with hand tremor smoothing.
* **Zero Calibration:** Your holding angle becomes the center horizon each time the dots start (**Calibrate on Start**, on by default), or lock it manually with one click.
* **Console Motion Sensor (optional):** Lets the dots move at the HOME menu and in homebrew such as RetroArch too (off by default).
* **Show in Screenshots/Videos (optional):** Choose whether the dots appear in screenshots and video captures (off by default).
* **Language Support:** English and Turkish language support.

<div align="center">
  <img src="assets/KinestopEN.png" alt="Kinestop Screenshot" width="1000">
</div>

### 🧩 Step 1: How It Works
Kinestop is a single overlay (`kinestop.ovl`) with two modes:
* **Settings panel:** the normal Kinestop menu, where you start the dots and pick colors, patterns, sensitivity and so on.
* **Dots:** a transparent full-screen layer that shows only the dots. Your game keeps full control of the buttons and the touch screen.

The dots are drawn at 640×360 and the Switch's display hardware scales them up to full screen, so **Kinestop works with Ultrahand's default 4MB overlay memory**.

The Switch can show only **one overlay at a time**. While the Kinestop panel, the Ultrahand menu or another overlay (e.g. Status Monitor) is open, the dots are not shown.

*Coming from Kinestop 1.x?* You can set Ultrahand's **Overlay Memory** back to **4MB**. Your existing `config.ini` is kept.

### 💾 Step 2: Installation
**Requirements:** You MUST have [nx-ovlloader](https://github.com/ppkantorski/nx-ovlloader) and [Ultrahand-Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) installed for this app to work!

**Method 1: Homebrew App Store (Automatic Installation)**
The HB App Store currently provides the **original upstream version by SertAy** (requires the 8MB Ultrahand memory setting), not this modified version.

<div align="center">
   <a href="https://hb-app.store/switch/Kinestop">
  <img src="assets/hbappstore.png" alt="HB App Store" width="250">
  <br>
  <a href="https://apps.fortheusers.org/switch/Kinestop">HB Store Download Link</a>
</div>
<br>

**Method 2: Manual Installation**
Download the `Kinestop.zip` release of this version (or build it yourself, see *Building from source* below), extract it and drag all the contents directly to the root of your SD card. It contains a single file, already in its correct folder: `SD:/switch/.overlays/kinestop.ovl`.

No reboot is needed after installing (unless you are removing an earlier 2.0.0 test build, see above). Your settings are stored in `SD:/config/kinestop/config.ini`, which is created automatically; the release does not include one, so updating never resets your settings.

### 🎮 Step 3: How to Use
1. **Open UltraHand:** While in-game, swipe from the left edge or press the Ultrahand key combo (**ZL + ZR + D-Pad Down** by default).
2. **Launch Kinestop:** Select Kinestop from the overlay list. The settings panel opens with the cursor on **Motion Dots**.
3. **Configure:** Choose your preferred color, pattern, size, opacity and sensitivity. Changes are saved automatically. The dots are not shown while the panel is open: select **Motion Dots** to see your changes. When the dots start, the way you are holding the console becomes the center (**Calibrate on Start**); turn that off to keep a fixed center set with **Calibrate Zero Position**.
4. **Play:** Select **Motion Dots** (the first item of the panel). The panel closes at once and the dots are shown over your game.
5. **Change settings:** Press the Ultrahand key combo (**ZL + ZR + D-Pad Down** by default, or the combo you set in Ultrahand). The dots stop when you press it, and the Kinestop settings panel opens when you let go, with the cursor on **Motion Dots**: select it to go back to the dots. (Swiping from the left edge does not work while the dots are shown.)
6. **Leave Kinestop:** Press **(B)** in the panel. Kinestop closes (the dots stay off) and you are back in the Ultrahand menu. In **Guide & Info**, **(B)** first goes back to the panel. Pressing the key combo while the panel is open only hides it, as with any overlay.

**Other overlays:** opening another overlay (for example with its own key combo) replaces the dots. To get them back, open Kinestop again and select **Motion Dots**.

**Motion Dots launch mode:** Kinestop registers a **Motion Dots** launch mode with Ultrahand (in Kinestop's overlay settings in Ultrahand, under launch modes; it is called **Hareket Noktaları** while Kinestop is set to Turkish). It starts the dots directly and can be given its own key combo; pressing that combo again while the dots are shown opens the Kinestop panel.

**Console Motion Sensor:** the motion sensor normally used by Kinestop does not report movement while you are at the HOME menu or in homebrew such as RetroArch, so the dots stand still there. Turning on **Console Motion Sensor** adds a second sensor that keeps working in those places, but it updates more slowly (the dots move in small steps) and uses 512 KB of overlay memory. It is off by default. Smoothing is time-based, so the dots react as quickly with this sensor as with the controllers'.

### 🛠️ Troubleshooting
* **The dots do not move at the HOME menu or in homebrew (e.g. RetroArch):** this is normal; turn on **Console Motion Sensor** if you want them to.
* **The dots disappear when I open another overlay:** this is normal, the Switch shows only one overlay at a time. Open Kinestop and select **Motion Dots** to bring them back.
* **Dots are missing from screenshots or videos:** enable **Show in Screenshots/Videos**. (Leave it off if you want clean captures.)
* **Kinestop crashes or does not open:** make sure the folder `SD:/atmosphere/contents/4200000000001042/` from an earlier 2.0.0 test build is deleted, and that nx-ovlloader and Ultrahand are up to date.
* **Ultrahand notifications look large while the dots are shown:** known cosmetic issue of the 640×360 dots layer; they look normal again in the panel and the Ultrahand menu.
* **Removing Kinestop:** delete `SD:/switch/.overlays/kinestop.ovl` and (optionally) `SD:/config/kinestop/` and the `[kinestop.ovl]` section of `SD:/config/ultrahand/overlays.ini`.

### 🧪 Test build (2.0.0-test.6)
Please set Ultrahand's **Overlay Memory** to **4MB** before testing, and delete the old sysmodule folder (see the top of this page). Then check, in handheld mode and docked if you can:
1. **Panel:** the Kinestop panel opens with the cursor on **Motion Dots**, and every setting works, including the language switch.
2. **Dots:** select **Motion Dots**. Does the panel disappear at once? Do the dots cover the whole screen? Do they move smoothly in a game? Does the game get all buttons and touches?
3. **Back to the panel:** press the Ultrahand combo (ZL + ZR + D-Pad Down by default). Do the dots stop, and does the Kinestop panel open when you let go, with the cursor on **Motion Dots** (test.5 often put it on Dot Opacity)? Try letting go of the buttons in different orders. Select **Motion Dots**: do the dots come back?
4. **B:** press **B** in the panel. Does the Ultrahand menu appear? Try it after opening Kinestop from the Ultrahand menu, after coming back from the dots with the combo, and after starting the dots with a **Motion Dots** launch-mode combo (step 10). In **Guide & Info**, **B** should go back to the panel.
5. **Hide:** with the panel open, press the Ultrahand combo. Does the panel hide like any overlay, and come back with the combo?
6. **HOME menu / homebrew:** with **Console Motion Sensor** off the dots should stand still at the HOME menu and in RetroArch; with it on they should move (in steps). Do they react about as quickly as in a game?
7. **Sleep:** with the dots on (try both with **Console Motion Sensor** on and off), press the Power button to sleep and wake the console. Does anything crash? Are the dots still there?
8. **Other overlays:** with the dots on, open another overlay (e.g. Status Monitor) with its own combo, then close it. What happens?
9. **Captures:** take a screenshot with **Show in Screenshots/Videos** off and on.
10. **Launch mode:** start **Motion Dots** from Kinestop's launch modes in Ultrahand (and, if you like, give it a key combo). Do the dots start? Press its combo again while the dots are shown: does the panel open, and where is the cursor? Do Ultrahand notifications that appear while the dots are shown look right?

Please report what you saw for each step, plus your HOS, Atmosphère and Ultrahand versions.

### 🔧 Building from source
Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` package (libnx), Python 3 and a host C++20 compiler for the tests.
```sh
make          # builds overlay/kinestop.ovl
make dist     # lays out an SD-card-ready tree in sdcard/
make zip      # same, plus Kinestop.zip
make test     # host-side tests (no devkitPro needed)
make clean
```

---

## 🔗 Contact and Donate

### SertAy

| Links | QR Code |
| :--- | :--- |
| **All Links:** [My Linktree](https://linktr.ee/yamanx) | <a href="https://linktr.ee/yamanx"><img src="assets/qr_SertAyTumLinkler.png" width="120"/></a> |

---

## (TR) Kurulum ve Kullanım Rehberi

### ✨ Özellikler
* **Taşıt Tutmasını Önleme:** Fiziksel hareketle senkronize hareket noktalarıyla (60 FPS'ye kadar) mide bulantısını engeller.
* **Tüm Oyunlarla Uyumlu:** Bütün oyunlarda ve Switch Ana Menüsünde çalışır.
* **Varsayılan 4MB Yeterli:** UltraHand bellek ayarını değiştirmenize gerek yoktur.
* **Serbest Hareket Modu:** Noktaların pencereden bakar gibi ekran dışına doğal süzülmesini sağlar.
* **Görsel Kişiselleştirme:** 12 renk teması, 6 yerleşim şablonu, boyut ve saydamlık seçenekleri.
* **Hassasiyet Ayarı:** 10 kademeli hassasiyet (0.2x - 6.0x) ve el titremesini filtreleme desteği.
* **Sıfır Noktası Kalibrasyonu:** Noktalar her başladığında tutuş açınız merkez ufuk seviyesi olur (**Başlarken Kalibre Et**, varsayılan açık) ya da tek tuşla elle sabitleyebilirsiniz.
* **Konsol Hareket Sensörü (isteğe bağlı):** Noktaların Ana Menüde ve RetroArch gibi homebrew'larda da hareket etmesini sağlar (varsayılan: kapalı).
* **Görüntü ve Kayıtlarda Göster (isteğe bağlı):** Noktaların ekran görüntülerinde ve video kayıtlarında görünüp görünmeyeceğini seçin (varsayılan: kapalı).
* **Dil Desteği:** Türkçe ve İngilizce dil desteği.

<div align="center">
  <img src="assets/KinestopTR.png" alt="Kinestop Ekran Görüntüsü" width="1000">
</div>

### 🧩 1. Aşama: Nasıl Çalışır?
Kinestop, iki modu olan tek bir overlay'dir (`kinestop.ovl`):
* **Ayar paneli:** Noktaları başlattığınız, renk, şablon, hassasiyet gibi ayarları seçtiğiniz bildiğimiz Kinestop menüsü.
* **Noktalar:** Yalnızca noktaları gösteren, tam ekran ve saydam bir katman. Tuşlar ve dokunmatik ekran tamamen oyunda kalır.

Noktalar 640×360 çözünürlükte çizilir ve Switch'in ekran donanımı bunları tam ekrana büyütür. Bu sayede **Kinestop, UltraHand'in varsayılan 4MB overlay belleğiyle çalışır.**

Switch aynı anda **yalnızca bir overlay** gösterebilir. Kinestop paneli, UltraHand menüsü ya da başka bir overlay (ör. Status Monitor) açıkken noktalar görünmez.

*Kinestop 1.x'ten mi geçiyorsunuz?* UltraHand'deki **Overlay Memory** ayarını tekrar **4MB**'a düşürebilirsiniz. Mevcut `config.ini` dosyanız korunur.

### 💾 2. Aşama: Kurulum
**Zorunlu Gereksinimler:** Uygulamanın çalışması için cihazınızda [nx-ovlloader](https://github.com/ppkantorski/nx-ovlloader) ve [Ultrahand-Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) yüklü olmak ZORUNDADIR!

**Yöntem 1: Homebrew App Store (Otomatik Kurulum)**
HB App Store'da şu an bu değiştirilmiş sürüm değil, **SertAy'ın orijinal sürümü** bulunmaktadır (UltraHand bellek ayarının 8MB yapılmasını gerektirir).

<div align="center">
   <a href="https://hb-app.store/switch/Kinestop">
  <img src="assets/hbappstore.png" alt="HB App Store" width="250">
  <br>
  <a href="https://apps.fortheusers.org/switch/Kinestop">HB Store İndirme Linki</a>
</div>
<br>

**Yöntem 2: Manuel Kurulum**
Bu sürümün `Kinestop.zip` dosyasını indirin (ya da aşağıdaki *Kaynak koddan derleme* bölümüne göre kendiniz derleyin), bilgisayarda çıkartın ve çıkan tüm dosyaları doğrudan SD kartınızın ana dizinine sürükleyip bırakın. Pakette doğru klasöre yerleştirilmiş tek bir dosya vardır: `SD:/switch/.overlays/kinestop.ovl`.

Kurulumdan sonra yeniden başlatmanız gerekmez (önceki bir 2.0.0 deneme sürümünü kaldırıyorsanız yukarıya bakın). Ayarlarınız otomatik oluşturulan `SD:/config/kinestop/config.ini` dosyasında saklanır. Sürüm paketinde bu dosya bulunmadığı için güncelleme yaptığınızda ayarlarınız sıfırlanmaz.

### 🎮 3. Aşama: Uygulamanın Kullanımı
1. **UltraHand'i Açın:** Ekranın sol kenarından sağa kaydırarak veya UltraHand kısayoluyla (varsayılan: **ZL + ZR + D-Pad Aşağı**) UltraHand'i açın.
2. **Kinestop'u Başlatın:** Listeden Kinestop'u seçin. Ayar paneli, imleç **Hareket Noktaları** üzerindeyken açılır.
3. **Ayarlayın:** İstediğiniz rengi, yerleşim şablonunu, boyutu, saydamlığı ve hassasiyeti belirleyin. Değişiklikler otomatik kaydedilir. Panel açıkken noktalar görünmez: değişiklikleri görmek için **Hareket Noktaları**'nı seçin. Noktalar başlarken konsolu tutuş şekliniz merkez olur (**Başlarken Kalibre Et**); sabit bir merkez için bunu kapatıp **Sıfır Noktasını Kalibre Et**'i kullanın.
4. **Oyuna Dönün:** **Hareket Noktaları**'nı (panelin ilk seçeneği) seçin. Panel hemen kapanır ve noktalar oyununuzun üzerinde görünür.
5. **Ayarları Değiştirin:** UltraHand kısayoluna basın (varsayılan: **ZL + ZR + D-Pad Aşağı** ya da UltraHand'de ayarladığınız kısayol). Bastığınızda noktalar durur, tuşları bıraktığınızda Kinestop ayar paneli imleç **Hareket Noktaları** üzerindeyken açılır: noktalara dönmek için onu seçin. (Noktalar ekrandayken sol kenardan kaydırma çalışmaz.)
6. **Kinestop'tan Çıkın:** Panelde **(B)** tuşuna basın. Kinestop kapanır (noktalar kapalı kalır) ve UltraHand menüsüne dönersiniz. **Yardım ve Rehber** sayfasında **(B)** önce panele döner. Panel açıkken kısayola basmak, her overlay'de olduğu gibi paneli yalnızca gizler.

**Diğer overlay'ler:** Başka bir overlay açtığınızda (ör. kendi kısayoluyla) noktaların yerini o alır. Noktaları geri getirmek için Kinestop'u yeniden açıp **Hareket Noktaları**'nı seçin.

**Hareket Noktaları başlatma modu:** Kinestop, UltraHand'e bir **Hareket Noktaları** başlatma modu ekler (UltraHand'de Kinestop'un overlay ayarlarında, başlatma modları altında; Kinestop İngilizceye ayarlıysa adı **Motion Dots** olur). Bu mod noktaları doğrudan başlatır ve ona ayrı bir kısayol atanabilir; noktalar ekrandayken bu kısayola yeniden basmak Kinestop panelini açar.

**Konsol Hareket Sensörü:** Kinestop'un normalde kullandığı hareket sensörü Ana Menüde ve RetroArch gibi homebrew'larda hareket bildirmez, bu yüzden noktalar orada sabit kalır. **Konsol Hareket Sensörü** seçeneği, bu yerlerde de çalışan ikinci bir sensörü devreye sokar; ancak bu sensör daha seyrek güncellenir (noktalar küçük adımlarla hareket eder) ve 512 KB overlay belleği kullanır. Varsayılan olarak kapalıdır. Yumuşatma zamana bağlı çalıştığı için noktalar bu sensörle de kumandalardaki kadar hızlı tepki verir.

### 🛠️ Sorun Giderme
* **Noktalar Ana Menüde veya homebrew'larda (ör. RetroArch) hareket etmiyor:** Bu normaldir; hareket etmelerini istiyorsanız **Konsol Hareket Sensörü** seçeneğini açın.
* **Başka bir overlay açınca noktalar kayboluyor:** Bu normaldir, Switch aynı anda yalnızca bir overlay gösterebilir. Geri getirmek için Kinestop'u açıp **Hareket Noktaları**'nı seçin.
* **Noktalar ekran görüntülerinde veya videolarda görünmüyor:** **Görüntü ve Kayıtlarda Göster** seçeneğini açın. (Temiz kayıt istiyorsanız kapalı bırakın.)
* **Kinestop çöküyor ya da açılmıyor:** Önceki bir 2.0.0 deneme sürümünden kalan `SD:/atmosphere/contents/4200000000001042/` klasörünün silindiğinden, nx-ovlloader ve UltraHand'in güncel olduğundan emin olun.
* **Noktalar ekrandayken UltraHand bildirimleri büyük görünüyor:** 640×360 nokta katmanından kaynaklanan bilinen bir görüntü sorunudur; panelde ve UltraHand menüsünde normal görünürler.
* **Kinestop'u kaldırma:** `SD:/switch/.overlays/kinestop.ovl` dosyasını, (isteğe bağlı olarak) `SD:/config/kinestop/` klasörünü ve `SD:/config/ultrahand/overlays.ini` dosyasındaki `[kinestop.ovl]` bölümünü silin.

### 🧪 Deneme sürümü (2.0.0-test.6)
Teste başlamadan önce UltraHand'in **Overlay Memory** ayarını **4MB** yapın ve eski sistem modülü klasörünü silin (bu sayfanın başına bakın). Ardından, el modunda ve mümkünse dock modunda şunları kontrol edin:
1. **Panel:** Kinestop paneli imleç **Hareket Noktaları** üzerindeyken açılıyor ve dil değiştirme dahil bütün ayarlar çalışıyor mu?
2. **Noktalar:** **Hareket Noktaları**'nı seçin. Panel hemen kayboluyor mu? Noktalar bütün ekranı kaplıyor mu? Oyunda akıcı hareket ediyorlar mı? Bütün tuşlar ve dokunmalar oyuna gidiyor mu?
3. **Panele dönüş:** UltraHand kısayoluna basın (varsayılan: ZL + ZR + D-Pad Aşağı). Noktalar duruyor ve tuşları bırakınca Kinestop paneli, imleç **Hareket Noktaları** üzerindeyken açılıyor mu (test.5'te imleç çoğu zaman Saydamlık'a gidiyordu)? Tuşları farklı sıralarla bırakmayı deneyin. **Hareket Noktaları**'nı seçince noktalar geri geliyor mu?
4. **B:** Panelde **B**'ye basın. UltraHand menüsü görünüyor mu? Kinestop'u UltraHand menüsünden açtıktan sonra, noktalardan kısayolla döndükten sonra ve noktaları **Hareket Noktaları** başlatma modu kısayoluyla başlattıktan sonra (10. adım) deneyin. **Yardım ve Rehber** sayfasında **B** panele dönmeli.
5. **Gizleme:** Panel açıkken UltraHand kısayoluna basın. Panel her overlay gibi gizleniyor ve kısayolla geri geliyor mu?
6. **Ana Menü / homebrew:** **Konsol Hareket Sensörü** kapalıyken noktalar Ana Menüde ve RetroArch'ta sabit kalmalı; açıkken (adım adım) hareket etmeli. Tepki hızı bir oyundakine yakın mı?
7. **Uyku modu:** Noktalar açıkken (**Konsol Hareket Sensörü** hem açık hem kapalıyken deneyin) Güç tuşuyla konsolu uyutup uyandırın. Bir çökme oluyor mu? Noktalar hâlâ ekranda mı?
8. **Diğer overlay'ler:** Noktalar açıkken başka bir overlay'i (ör. Status Monitor) kendi kısayoluyla açıp kapatın. Ne oluyor?
9. **Kayıtlar:** **Görüntü ve Kayıtlarda Göster** kapalıyken ve açıkken birer ekran görüntüsü alın.
10. **Başlatma modu:** UltraHand'de Kinestop'un başlatma modlarından **Hareket Noktaları**'nı başlatın (isterseniz bir kısayol da atayın). Noktalar başlıyor mu? Noktalar ekrandayken kısayola yeniden basın: panel açılıyor mu, imleç nerede? Noktalar ekrandayken gelen UltraHand bildirimleri düzgün görünüyor mu?

Her adımda ne gördüğünüzü, HOS, Atmosphère ve UltraHand sürümlerinizle birlikte bildirin.

### 🔧 Kaynak koddan derleme
`switch-dev` paketiyle (libnx) birlikte [devkitPro](https://devkitpro.org/wiki/Getting_Started), Python 3 ve testler için bilgisayarda C++20 destekli bir derleyici gerekir.
```sh
make          # overlay/kinestop.ovl dosyasını derler
make dist     # SD karta kopyalanmaya hazır dosya ağacını sdcard/ içine oluşturur
make zip      # aynısı, ek olarak Kinestop.zip
make test     # bilgisayarda çalışan testler (devkitPro gerekmez)
make clean
```

---

## 🔗 İletişim ve Bağış

### SertAy

| Linkler | QR Kod |
| :--- | :--- |
| **Tüm Linkler:** [Linktree Sayfam](https://linktr.ee/yamanx) | <a href="https://linktr.ee/yamanx"><img src="assets/qr_SertAyTumLinkler.png" width="120"/></a> |

---

## 📄 License / Lisans

Telif Hakkı — © 2026 **SertAy**.  
Bu proje kişisel ve ticari olmayan kullanım için açıktır. Projenin kaynak belirtilmeksizin paylaşılması, sahiplenilmesi veya satılması kesinlikle yasaktır. Detaylar için lütfen [LICENSE](LICENSE) dosyasını inceleyiniz.  
Bu depo, orijinal [Kinestop](https://github.com/sertay1/Kinestop) projesinin değiştirilmiş bir sürümüdür; değişiklikler bu dosyanın başında listelenmiştir.

Copyright (c) 2026 **SertAy**.  
This project is available for personal and non-commercial use with mandatory author attribution. Claiming authorship, unauthorized redistribution, or selling this software is strictly prohibited. Please see the [LICENSE](LICENSE) file for details.  
This repository is a modified version of the original [Kinestop](https://github.com/sertay1/Kinestop); the modifications are listed at the top of this file.

The bundled [libultrahand](https://github.com/ppkantorski/libultrahand) (including libtesla) keeps its own GPL-2.0 license; the small Kinestop changes to it are marked `Kinestop patch`.
