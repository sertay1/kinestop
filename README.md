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
> * The settings panel and the dots are two modes of the same overlay (`kinestop.ovl`): leaving the panel with **B** switches to the dots, and the Ultrahand key combo switches back to the panel.
> * The dots are not shown while another overlay or the Ultrahand menu is open, because the Switch can show only one overlay layer at a time. Leaving the Kinestop panel brings them back; after another overlay, open Kinestop and press **B**.
> * The settings panel no longer shows a live preview of the dots behind it; press **B** to see your changes.
> * From the dots, only the Ultrahand key combo opens the settings; the original's left-edge touch swipe and extra button combos were removed. A new **Ultrahand Menu** item in the panel opens the Ultrahand menu without turning the dots off.
> * Kinestop adds a **Motion Dots** launch mode to its entry in Ultrahand's `overlays.ini`, so Ultrahand can start the dots directly (and give them their own key combo).
> * New options: **Console Motion Sensor** (lets the dots move at the HOME menu) and **Show in Screenshots/Videos**. The unused config keys `draw_center_reticle` / `draw_roll_indicators` are ignored.
> * A small patch to the bundled libultrahand/libtesla (marked `Kinestop patch` in the source) for the full-screen scaled layer and the motion sensor.
>
> **Bu, [SertAy'ın Kinestop](https://github.com/sertay1/Kinestop) projesinin değiştirilmiş bir sürümüdür.** Orijinal fikir, tasarım, hareket algoritması, renk temaları ve rehberin tüm hakları **SertAy**'a aittir. Bu sürüm, UltraHand'in **varsayılan 4MB** overlay belleğiyle çalışacak şekilde optimize edilmiştir: noktalar 640×360 çözünürlükte çizilip Switch'in ekran donanımıyla tam ekrana büyütülür; ayar paneli ve noktalar aynı overlay'in iki modudur; Switch aynı anda yalnızca bir overlay katmanı gösterebildiği için başka bir overlay veya UltraHand menüsü açıkken noktalar görünmez (Kinestop panelinden çıkınca geri gelirler, başka bir overlay'den sonra Kinestop'u açıp **B**'ye basın); ayar paneli artık arkasında noktaların canlı önizlemesini göstermez (değişiklikleri görmek için **B**'ye basın); noktalardan ayarlara yalnızca UltraHand kısayoluyla dönülür, orijinaldeki sol kenardan kaydırma ve ek tuş kombinasyonları kaldırılmıştır; panele, noktaları kapatmadan UltraHand menüsünü açan **Ultrahand Menüsü** seçeneği eklenmiştir; Kinestop, UltraHand'in `overlays.ini` dosyasındaki kendi kaydına noktaları doğrudan başlatan bir **Motion Dots** başlatma modu ekler; **Konsol Hareket Sensörü** ve **Görüntü ve Kayıtlarda Göster** seçenekleri eklenmiş, kullanılmayan `draw_center_reticle` / `draw_roll_indicators` ayar anahtarları yok sayılmaktadır; birlikte gelen libultrahand/libtesla kütüphanesine küçük bir yama yapılmıştır (kaynakta `Kinestop patch` ile işaretli). Değişiklikler [LICENSE](LICENSE) dosyasının 2. maddesi gereği burada listelenmiştir.

> [!WARNING]
> **Status:** version **2.0.0-test.5** is a **test build** and has not been tested on a console yet. The earlier 2.0.0 test builds drew the dots from a separate sysmodule; console tests showed that the Switch never displays that layer together with an overlay menu, so that design was dropped. This build goes back to a single overlay, like the original, but fits in the default 4MB. See **Test build (2.0.0-test.5)** below for what to try. The [original release](https://github.com/sertay1/Kinestop/releases) is the stable version.
>
> **Upgrading from an earlier 2.0.0 test build:** delete the folder `SD:/atmosphere/contents/4200000000001042/` (the old sysmodule, including its `flags/boot2.flag`) and, if you like, `SD:/config/kinestop/sysmodule.log`, then reboot. Your `config.ini` is kept; the old test-build `debug_flags` setting in it is ignored.
>
> **Durum:** **2.0.0-test.5** bir **deneme sürümüdür** ve henüz konsolda test edilmedi. Önceki 2.0.0 deneme sürümleri noktaları ayrı bir sistem modülüyle (sysmodule) çiziyordu; konsol testleri, Switch'in bu katmanı bir overlay menüsüyle hiçbir zaman aynı anda göstermediğini ortaya koydu, bu yüzden o tasarımdan vazgeçildi. Bu sürüm orijinal gibi tek bir overlay'e geri döner, ama varsayılan 4MB belleğe sığar. Ne denenmesi gerektiği aşağıdaki **Deneme sürümü (2.0.0-test.5)** bölümündedir. Kararlı sürüm için [orijinal sürüme](https://github.com/sertay1/Kinestop/releases) bakın.
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
* **Zero Calibration:** Lock your current holding angle as the center horizon with one click.
* **Console Motion Sensor (optional):** Lets the dots move at the HOME menu too (off by default).
* **Show in Screenshots/Videos (optional):** Choose whether the dots appear in screenshots and video captures (off by default).
* **Language Support:** English and Turkish language support.

<div align="center">
  <img src="assets/KinestopEN.png" alt="Kinestop Screenshot" width="1000">
</div>

### 🧩 Step 1: How It Works
Kinestop is a single overlay (`kinestop.ovl`) with two modes:
* **Settings panel:** the normal Kinestop menu, where you turn the dots on and pick colors, patterns, sensitivity and so on.
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
2. **Launch Kinestop:** Select Kinestop from the overlay list. The settings panel opens.
3. **Turn on the dots:** Enable **Enable Motion Dots**.
4. **Configure:** Choose your preferred color, pattern, size, opacity and sensitivity. Changes are saved automatically. The dots are not shown while the panel is open: press **(B)** to see your changes. Hold the console the way you play and use **Calibrate Zero Position** to set the center.
5. **Play:** Press **(B)**. Kinestop switches to the dots and you are back in your game.
6. **Change settings:** Press the Ultrahand key combo (**ZL + ZR + D-Pad Down** by default, or the combo you set in Ultrahand). The Kinestop settings panel opens; press **(B)** again to go back to the dots. (Swiping from the left edge does not work while the dots are shown.)
7. **Turn the dots off:** Open the settings panel with the combo, turn **Enable Motion Dots** off and press **(B)**. Kinestop closes and you are back in the Ultrahand menu.

**Other overlays:** opening another overlay (for example with its own key combo) replaces the dots. To get them back, open Kinestop again and press **(B)**. To reach the Ultrahand menu without turning the dots off, select **Ultrahand Menu** in the panel.

**Motion Dots launch mode:** Kinestop registers a **Motion Dots** launch mode with Ultrahand (in Kinestop's overlay settings in Ultrahand, under launch modes). It starts the dots directly and can be given its own key combo. If **Enable Motion Dots** is off, it opens the settings panel instead.

**Console Motion Sensor:** the motion sensor normally used by Kinestop does not report movement while you are at the HOME menu, so the dots stand still there. Turning on **Console Motion Sensor** adds a second sensor that keeps working at the HOME menu, but it updates more slowly (the dots move in small steps) and uses 512 KB of overlay memory. It is off by default.

### 🛠️ Troubleshooting
* **The dots do not move at the HOME menu:** this is normal; turn on **Console Motion Sensor** if you want them to.
* **The dots disappear when I open another overlay:** this is normal, the Switch shows only one overlay at a time. Open Kinestop and press **(B)** to bring them back.
* **Dots are missing from screenshots or videos:** enable **Show in Screenshots/Videos**. (Leave it off if you want clean captures.)
* **Kinestop crashes or does not open:** make sure the folder `SD:/atmosphere/contents/4200000000001042/` from an earlier 2.0.0 test build is deleted, and that nx-ovlloader and Ultrahand are up to date.
* **Ultrahand notifications look large while the dots are shown:** known cosmetic issue of the 640×360 dots layer; they look normal again in the panel and the Ultrahand menu.
* **Removing Kinestop:** delete `SD:/switch/.overlays/kinestop.ovl` and (optionally) `SD:/config/kinestop/` and the `[kinestop.ovl]` section of `SD:/config/ultrahand/overlays.ini`.

### 🧪 Test build (2.0.0-test.5)
Please set Ultrahand's **Overlay Memory** to **4MB** before testing, and delete the old sysmodule folder (see the top of this page). Then check, in handheld mode and docked if you can:
1. **Panel:** the Kinestop panel opens and every setting works, including the language switch.
2. **Dots:** turn the dots on and press **B**. Do the dots cover the whole screen? Do they move smoothly in a game? Does the game get all buttons and touches?
3. **Back to the panel:** press the Ultrahand combo (ZL + ZR + D-Pad Down by default). Does the Kinestop panel open? Press **B**: do the dots come back? Select **Ultrahand Menu**: does the Ultrahand menu open?
4. **Dots off:** turn the dots off in the panel and press **B**. Does the Ultrahand menu appear (also when Kinestop was opened with a key combo)?
5. **HOME menu:** with **Console Motion Sensor** off the dots should stand still at the HOME menu; with it on they should move (in steps).
6. **Sleep:** with the dots on (try both with **Console Motion Sensor** on and off), press the Power button to sleep and wake the console. Does anything crash? Are the dots still there?
7. **Other overlays:** with the dots on, open another overlay (e.g. Status Monitor) with its own combo, then close it. What happens?
8. **Captures:** take a screenshot with **Show in Screenshots/Videos** off and on.
9. **Launch mode:** start **Motion Dots** from Kinestop's launch modes in Ultrahand (and, if you like, give it a key combo). Do the dots start? Do Ultrahand notifications that appear while the dots are shown look right?

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
* **Sıfır Noktası Kalibrasyonu:** Konsolu tuttuğunuz açıyı tek tuşla merkez ufuk seviyesi yapar.
* **Konsol Hareket Sensörü (isteğe bağlı):** Noktaların Ana Menüde de hareket etmesini sağlar (varsayılan: kapalı).
* **Görüntü ve Kayıtlarda Göster (isteğe bağlı):** Noktaların ekran görüntülerinde ve video kayıtlarında görünüp görünmeyeceğini seçin (varsayılan: kapalı).
* **Dil Desteği:** Türkçe ve İngilizce dil desteği.

<div align="center">
  <img src="assets/KinestopTR.png" alt="Kinestop Ekran Görüntüsü" width="1000">
</div>

### 🧩 1. Aşama: Nasıl Çalışır?
Kinestop, iki modu olan tek bir overlay'dir (`kinestop.ovl`):
* **Ayar paneli:** Noktaları açtığınız, renk, şablon, hassasiyet gibi ayarları seçtiğiniz bildiğimiz Kinestop menüsü.
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
2. **Kinestop'u Başlatın:** Listeden Kinestop'u seçin. Ayar paneli açılır.
3. **Noktaları Açın:** **Hareket Noktalarını Aç** seçeneğini açın.
4. **Ayarlayın:** İstediğiniz rengi, yerleşim şablonunu, boyutu, saydamlığı ve hassasiyeti belirleyin. Değişiklikler otomatik kaydedilir. Panel açıkken noktalar görünmez: değişiklikleri görmek için **(B)** tuşuna basın. Konsolu oynadığınız gibi tutup **Sıfır Noktasını Kalibre Et** ile merkezi belirleyin.
5. **Oyuna Dönün:** **(B)** tuşuna basın. Kinestop noktalara geçer ve oyununuza dönersiniz.
6. **Ayarları Değiştirin:** UltraHand kısayoluna basın (varsayılan: **ZL + ZR + D-Pad Aşağı** ya da UltraHand'de ayarladığınız kısayol). Kinestop ayar paneli açılır; noktalara dönmek için yine **(B)** tuşuna basın. (Noktalar ekrandayken sol kenardan kaydırma çalışmaz.)
7. **Noktaları Kapatın:** Kısayolla ayar panelini açın, **Hareket Noktalarını Aç** seçeneğini kapatın ve **(B)** tuşuna basın. Kinestop kapanır ve UltraHand menüsüne dönersiniz.

**Diğer overlay'ler:** Başka bir overlay açtığınızda (ör. kendi kısayoluyla) noktaların yerini o alır. Noktaları geri getirmek için Kinestop'u yeniden açıp **(B)** tuşuna basın. Noktaları kapatmadan UltraHand menüsüne gitmek için paneldeki **Ultrahand Menüsü** seçeneğini kullanın.

**Motion Dots başlatma modu:** Kinestop, UltraHand'e bir **Motion Dots** başlatma modu ekler (UltraHand'de Kinestop'un overlay ayarlarında, başlatma modları altında). Bu mod noktaları doğrudan başlatır ve ona ayrı bir kısayol atanabilir. **Hareket Noktalarını Aç** kapalıysa bunun yerine ayar paneli açılır.

**Konsol Hareket Sensörü:** Kinestop'un normalde kullandığı hareket sensörü Ana Menüdeyken hareket bildirmez, bu yüzden noktalar orada sabit kalır. **Konsol Hareket Sensörü** seçeneği, Ana Menüde de çalışan ikinci bir sensörü devreye sokar; ancak bu sensör daha seyrek güncellenir (noktalar küçük adımlarla hareket eder) ve 512 KB overlay belleği kullanır. Varsayılan olarak kapalıdır.

### 🛠️ Sorun Giderme
* **Noktalar Ana Menüde hareket etmiyor:** Bu normaldir; hareket etmelerini istiyorsanız **Konsol Hareket Sensörü** seçeneğini açın.
* **Başka bir overlay açınca noktalar kayboluyor:** Bu normaldir, Switch aynı anda yalnızca bir overlay gösterebilir. Geri getirmek için Kinestop'u açıp **(B)** tuşuna basın.
* **Noktalar ekran görüntülerinde veya videolarda görünmüyor:** **Görüntü ve Kayıtlarda Göster** seçeneğini açın. (Temiz kayıt istiyorsanız kapalı bırakın.)
* **Kinestop çöküyor ya da açılmıyor:** Önceki bir 2.0.0 deneme sürümünden kalan `SD:/atmosphere/contents/4200000000001042/` klasörünün silindiğinden, nx-ovlloader ve UltraHand'in güncel olduğundan emin olun.
* **Noktalar ekrandayken UltraHand bildirimleri büyük görünüyor:** 640×360 nokta katmanından kaynaklanan bilinen bir görüntü sorunudur; panelde ve UltraHand menüsünde normal görünürler.
* **Kinestop'u kaldırma:** `SD:/switch/.overlays/kinestop.ovl` dosyasını, (isteğe bağlı olarak) `SD:/config/kinestop/` klasörünü ve `SD:/config/ultrahand/overlays.ini` dosyasındaki `[kinestop.ovl]` bölümünü silin.

### 🧪 Deneme sürümü (2.0.0-test.5)
Teste başlamadan önce UltraHand'in **Overlay Memory** ayarını **4MB** yapın ve eski sistem modülü klasörünü silin (bu sayfanın başına bakın). Ardından, el modunda ve mümkünse dock modunda şunları kontrol edin:
1. **Panel:** Kinestop paneli açılıyor ve dil değiştirme dahil bütün ayarlar çalışıyor mu?
2. **Noktalar:** Noktaları açıp **B**'ye basın. Noktalar bütün ekranı kaplıyor mu? Oyunda akıcı hareket ediyorlar mı? Bütün tuşlar ve dokunmalar oyuna gidiyor mu?
3. **Panele dönüş:** UltraHand kısayoluna basın (varsayılan: ZL + ZR + D-Pad Aşağı). Kinestop paneli açılıyor mu? **B**'ye basınca noktalar geri geliyor mu? **Ultrahand Menüsü**'nü seçince UltraHand menüsü açılıyor mu?
4. **Noktalar kapalı:** Panelden noktaları kapatıp **B**'ye basın. UltraHand menüsü görünüyor mu (Kinestop bir kısayolla açıldığında da)?
5. **Ana Menü:** **Konsol Hareket Sensörü** kapalıyken noktalar Ana Menüde sabit kalmalı; açıkken (adım adım) hareket etmeli.
6. **Uyku modu:** Noktalar açıkken (**Konsol Hareket Sensörü** hem açık hem kapalıyken deneyin) Güç tuşuyla konsolu uyutup uyandırın. Bir çökme oluyor mu? Noktalar hâlâ ekranda mı?
7. **Diğer overlay'ler:** Noktalar açıkken başka bir overlay'i (ör. Status Monitor) kendi kısayoluyla açıp kapatın. Ne oluyor?
8. **Kayıtlar:** **Görüntü ve Kayıtlarda Göster** kapalıyken ve açıkken birer ekran görüntüsü alın.
9. **Başlatma modu:** UltraHand'de Kinestop'un başlatma modlarından **Motion Dots**'u başlatın (isterseniz bir kısayol da atayın). Noktalar başlıyor mu? Noktalar ekrandayken gelen UltraHand bildirimleri düzgün görünüyor mu?

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
