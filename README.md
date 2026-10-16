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
> This version moves dot rendering out of the overlay and into a small background sysmodule. In line with section 2 of the [LICENSE](LICENSE), the modifications are:
> * A new sysmodule (`atmosphere/contents/4200000000001042`) that reads the motion sensors and draws the dots on its own display layer.
> * The overlay (`kinestop.ovl`) is now only a settings panel: it starts/stops the sysmodule and sends it your settings.
> * No more 8MB Ultrahand requirement; other overlays can be used while the dots are on.
> * New option: **Show in Screenshots/Videos**. Removed: center reticle / roll indicator settings.
>
> **Bu, [SertAy'ın Kinestop](https://github.com/sertay1/Kinestop) projesinin değiştirilmiş bir sürümüdür.** Orijinal fikir, tasarım, hareket algoritması, renk temaları ve rehberin tüm hakları **SertAy**'a aittir. Bu sürümde noktaların çizimi overlay'den alınıp arka planda çalışan küçük bir sistem modülüne (sysmodule) taşınmıştır. Değişiklikler [LICENSE](LICENSE) dosyasının 2. maddesi gereği yukarıda listelenmiştir.

> [!WARNING]
> **Status:** version 2.0.0 (the sysmodule split) is a **test build and does not work correctly yet**. The first console test (HOS 19.0.1, handheld) showed two problems: the dots appeared but did not move, and while they were on, overlay menus (the Kinestop panel and the Ultrahand menu) were not drawn. 2.0.0-test.2 showed that the dots do move, and that the menus stay visible as long as the dots are paused while a menu is open, which is what this build does by default. **2.0.0-test.3** reads the motion sensor at its full rate (the dots moved in steps in test.2), writes its log in the background, and adds a test that keeps the dots on while menus are open, to find out whether both can be shown at the same time. See **Test build (2.0.0-test.3)** below for what to try. **Normal use:** open the Kinestop panel from the Ultrahand menu, turn the dots on, then *hide* the panel with the Ultrahand launch combo; the dots come back. Pressing the combo again shows the panel (dots paused). Leaving the panel with **B** (back to the Ultrahand menu) keeps the dots paused so that the menu stays visible; to bring them back, open Kinestop again and hide it with the combo. Live diagnostics are shown in the panel and written to `SD:/config/kinestop/sysmodule.log`. The [original release](https://github.com/sertay1/Kinestop/releases) is the stable, overlay-only version.
>
> **Durum:** 2.0.0 sürümü (sysmodule ayrımı) bir **deneme sürümüdür ve henüz doğru çalışmıyor.** İlk konsol testinde (HOS 19.0.1, el modu) iki sorun görüldü: noktalar göründü ama hareket etmedi, ve noktalar açıkken overlay menüleri (Kinestop paneli ve Ultrahand menüsü) çizilmedi. 2.0.0-test.2, noktaların hareket ettiğini ve bir menü açıkken noktalar duraklatıldığı sürece menülerin görünür kaldığını gösterdi; bu sürüm de varsayılan olarak böyle çalışır. **2.0.0-test.3** hareket sensörünü tam hızında okur (test.2'de noktalar kesik kesik hareket ediyordu), günlüğü arka planda yazar ve noktaların menülerle aynı anda gösterilip gösterilemeyeceğini anlamak için menüler açıkken noktaları açık tutan bir test ekler. Ne denenmesi gerektiği aşağıdaki **Deneme sürümü (2.0.0-test.3)** bölümündedir. **Normal kullanım:** Ultrahand menüsünden Kinestop panelini açın, noktaları açın ve paneli Ultrahand açma kombinasyonuyla *gizleyin*; noktalar geri gelir. Kombinasyona tekrar basınca panel görünür (noktalar duraklatılır). Panelden **B** ile çıkarsanız (Ultrahand menüsüne dönüş) noktalar duraklatılmış kalır, böylece menü görünür kalır; geri getirmek için Kinestop'u tekrar açıp kombinasyonla gizleyin. Panelde canlı tanılama bilgisi gösterilir ve `SD:/config/kinestop/sysmodule.log` dosyasına yazılır. Kararlı, yalnızca overlay'den oluşan sürüm için [orijinal sürüme](https://github.com/sertay1/Kinestop/releases) bakın.

---

## (EN) Setup and Usage Guide

### ✨ Features
* **Prevents Motion Sickness:** Floating motion cues (up to 60 FPS) sync with physical movement to eliminate nausea.
* **Universal Compatibility:** Works across all games and the Switch Home Menu.
* **Runs in the Background:** Dots keep showing after you close the overlay, and other overlays (e.g. Status Monitor) can be opened at the same time.
* **Free Floating Mode:** Dots can drift naturally beyond screen edges for an authentic horizon view.
* **Visual Customization:** 12 color themes, 6 placement patterns, dot size, and opacity options.
* **Adjustable Sensitivity:** 10 sensitivity levels (0.2x - 6.0x) with hand tremor smoothing.
* **Zero Calibration:** Lock your current holding angle as the center horizon with one click.
* **Show in Screenshots/Videos (optional):** Choose whether the dots appear in screenshots and video captures (off by default).
* **Language Support:** English and Turkish language support.

<div align="center">
  <img src="assets/KinestopEN.png" alt="Kinestop Screenshot" width="1000">
</div>

### 🧩 Step 1: How It Works
Kinestop is made of two parts:
* **A background sysmodule** that reads the motion sensors and draws the dots on its own display layer. It draws at 640×360 and the Switch's display hardware scales that up to full screen, so it only uses **about 1.5 MB of system memory, and only while the dots are on**. When you turn the dots off, the sysmodule exits and frees that memory.
* **An overlay** (`kinestop.ovl`) that is just the control panel: turn the dots on/off, pick colors, patterns and sensitivity, and calibrate.

Because the overlay no longer draws anything, **it works with Ultrahand's default 4MB overlay memory** – no settings change needed – and **you can use other overlays while the dots are showing**.

*Coming from Kinestop 1.x?* You can set Ultrahand's **Overlay Memory** back to **4MB**. Your existing `config.ini` is kept.

### 💾 Step 2: Installation
**Requirements:** You MUST have [nx-ovlloader](https://github.com/ppkantorski/nx-ovlloader) and [Ultrahand-Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) installed for this app to work!

**Method 1: Homebrew App Store (Automatic Installation)**
The HB App Store currently provides the **original upstream version by SertAy** (overlay-only, requires the 8MB Ultrahand memory setting), not this modified version.

<div align="center">
   <a href="https://hb-app.store/switch/Kinestop">
  <img src="assets/hbappstore.png" alt="HB App Store" width="250">
  <br>
  <a href="https://apps.fortheusers.org/switch/Kinestop">HB Store Download Link</a>
</div>
<br>

**Method 2: Manual Installation**
Download the `Kinestop.zip` release of this version (or build it yourself, see *Building from source* below), extract it and drag all the contents directly to the root of your SD card. The files are already placed in their correct folder paths:
* `SD:/atmosphere/contents/4200000000001042/exefs.nsp` – the dot-drawing sysmodule
* `SD:/atmosphere/contents/4200000000001042/toolbox.json` – lets sysmodule managers (e.g. ovl-sysmodules) list, start and stop it; a start from there always shows the dots, whatever the Kinestop toggle was last set to
* `SD:/switch/.overlays/kinestop.ovl` – the settings overlay

No reboot is needed after installing. Your settings are stored in `SD:/config/kinestop/config.ini`, which is created automatically; the release does not include one, so updating never resets your settings.

### 🎮 Step 3: How to Use
1. **Open UltraHand:** While in-game, swipe from the left edge or press **L + R + D-Pad Down + Right Stick**.
2. **Launch Kinestop:** Select Kinestop from the overlay list.
3. **Turn on the dots:** The **Enable Motion Dots** toggle starts the background sysmodule; turning it off stops it and frees its memory.
4. **Configure:** Choose your preferred color, pattern, size, opacity and sensitivity. Changes apply live and are saved automatically.
5. **Play:** Press **(B)** to close the menu. **The dots keep running after the overlay is closed**, and you can open other overlays while they are on.

**Start at boot (optional):** If you want the dots without opening the overlay after every reboot, enable **Start Dots at Boot** in Kinestop's settings; the dots then start at every boot, even if you switched them off before shutting down. This creates `atmosphere/contents/4200000000001042/flags/boot2.flag`. It is off by default because boot-time sysmodules share a limited memory pool – see Troubleshooting if your console fails to boot afterwards.

**Status line:** the top of the overlay shows what the sysmodule is doing:
* **Running** – the dots are being drawn.
* **Stopped** – the sysmodule is not running (dots off).
* **Starting...** / **Stopping...** – the overlay has launched the sysmodule and is waiting for it to answer.
* **Running - no sensor data** – running, but no motion sensor readings yet (normal for a moment after starting).
* **Error** – a stage name (VI init, Display, Layer, Layer stack, NWindow, Framebuffer, Sensor) and an error code `2xxx-xxxx`. It clears by itself once that stage recovers. Please include these when reporting a problem.
* **API mismatch** – the overlay and the sysmodule are from different releases; copy both files from the same release.

The sysmodule writes a log to `SD:/config/kinestop/sysmodule.log`, which is the most useful thing to attach to a bug report.

### 🛠️ Troubleshooting
* **The status line shows an error / no dots appear:** note the stage and error code, turn the dots off and on again, and check `SD:/config/kinestop/sysmodule.log`.
* **Status stays on "Stopped", "Starting..." or "Start failed":** make sure `exefs.nsp` is in `SD:/atmosphere/contents/4200000000001042/` and that your Atmosphère is up to date. Other sysmodules using a lot of memory can also prevent it from starting; try disabling some.
* **Console fails to boot with error 2001-0132 after enabling Start Dots at Boot:** too many boot-time sysmodules are competing for system memory. Put the SD card in a PC and delete `SD:/atmosphere/contents/4200000000001042/flags/boot2.flag` (or disable other sysmodules). Kinestop works fine started from the overlay instead.
* **Dots are missing from screenshots or videos:** enable **Show in Screenshots/Videos**. (Leave it off if you want clean captures.)
* **"API mismatch":** you have mixed files from different releases; reinstall all three files from one release.
* **Removing Kinestop:** turn the dots off, then delete `SD:/atmosphere/contents/4200000000001042/`, `SD:/switch/.overlays/kinestop.ovl` and (optionally) `SD:/config/kinestop/`.

### 🧪 Test build (2.0.0-test.3)
The Kinestop panel has a **Diagnostics (test build)** section with these toggles. They are saved in `config.ini` as `debug_flags` (the sum of the values below), so they can also be set from a PC:

| Toggle | `debug_flags` | What it does |
|---|---|---|
| Blink test | 1 | Dots off 3 s out of every 10 s. |
| Unscaled test layer | 2 | Layer mode **Quarter 1x**: the 640×360 image at 1:1 in the top-left quarter of the screen (the whole pattern at half size). |
| Verbose log | 4 | One diagnostics line per second in `sysmodule.log`. |
| Keep dots while menus open (coexistence test) | 8 | The dots are **not** paused for the Kinestop panel or the Ultrahand menu. |
| Small 2x-scaled test layer | 16 | Layer mode **Quarter 2x**: a 320×180 image scaled 2x into the top-left quarter. If both layer toggles are on, *Unscaled* wins. |

With both layer toggles off the layer mode is **Full screen 2x** (the default): the 640×360 image scaled 2x to fill the screen. The first line of the diagnostics block (`Layer: ...`) shows the mode in use.

**test.3 checklist.** Turn **Verbose log** on and **Blink test** off, and turn the dots on. Then, for each layer mode (**Full screen 2x**, **Quarter 1x**, **Quarter 2x**):
1. Pick the layer mode, then turn **Keep dots while menus open** ON (leave it on for the next modes). Note whether **the panel** stays visible and whether **the dots** appear next to it (in the top-left quarter for the two Quarter modes).
2. Hide the panel with the combo and check that the dots move smoothly.
3. Open the Kinestop panel again with the combo. Note whether the panel and the dots are both visible.
4. Press **B**. Note whether **the Ultrahand menu** appears, and whether the dots are visible with it.
5. If you have **Status Monitor**, open it from the Ultrahand menu and note whether it and the dots are both visible.
6. Close the menus and note whether the dots are still there.

If a menu disappears, wait a few seconds; if it does not come back, **reboot**. With **Start Dots at Boot** off the dots stay off after the reboot, so you can open Kinestop and change the layer mode before turning the dots on again. *Keep dots while menus open* **stays on after a reboot**: turn it off in the panel before turning the dots on. If **Start Dots at Boot** is on (the dots are back, and opening the panel repeats the test) or the panel cannot be reached, set `debug_flags` in `SD:/config/kinestop/config.ini` to `4` from a PC.

Please report, per layer mode: panel visible? dots with the panel? Ultrahand menu after B? dots with the Ultrahand menu? Status Monitor? dots smooth? Attach `SD:/config/kinestop/sysmodule.log` (copy it before the next start of the dots, it is cleared then).

### 🔧 Building from source
Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` package (libnx) and Python 3.
```sh
make          # builds sysmodule/out/kinestop.nsp and overlay/kinestop.ovl
make dist     # lays out an SD-card-ready tree in sdcard/
make zip      # same, plus Kinestop.zip
make test     # host-side tests (if present)
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
* **Arka Planda Çalışır:** Overlay'i kapattıktan sonra da noktalar ekranda kalır; aynı anda başka overlay'leri (ör. Status Monitor) de açabilirsiniz.
* **Serbest Hareket Modu:** Noktaların pencereden bakar gibi ekran dışına doğal süzülmesini sağlar.
* **Görsel Kişiselleştirme:** 12 renk teması, 6 yerleşim şablonu, boyut ve saydamlık seçenekleri.
* **Hassasiyet Ayarı:** 10 kademeli hassasiyet (0.2x - 6.0x) ve el titremesini filtreleme desteği.
* **Sıfır Noktası Kalibrasyonu:** Konsolu tuttuğunuz açıyı tek tuşla merkez ufuk seviyesi yapar.
* **Görüntü ve Kayıtlarda Göster (isteğe bağlı):** Noktaların ekran görüntülerinde ve video kayıtlarında görünüp görünmeyeceğini seçin (varsayılan: kapalı).
* **Dil Desteği:** Türkçe ve İngilizce dil desteği.

<div align="center">
  <img src="assets/KinestopTR.png" alt="Kinestop Ekran Görüntüsü" width="1000">
</div>

### 🧩 1. Aşama: Nasıl Çalışır?
Kinestop iki parçadan oluşur:
* **Arka planda çalışan bir sistem modülü (sysmodule):** Hareket sensörlerini okur ve noktaları kendi ekran katmanına çizer. Çizimi 640×360 çözünürlükte yapar, Switch'in ekran donanımı bunu tam ekrana büyütür. Bu sayede **yalnızca noktalar açıkken, yaklaşık 1,5 MB sistem belleği** kullanır. Noktaları kapattığınızda modül kapanır ve bu belleği geri bırakır.
* **Bir overlay** (`kinestop.ovl`): Yalnızca kontrol panelidir. Noktaları açıp kapatır, renk, şablon ve hassasiyet seçmenizi ve kalibrasyon yapmanızı sağlar.

Overlay artık çizim yapmadığı için **UltraHand'in varsayılan 4MB overlay belleğiyle çalışır** (herhangi bir ayar değiştirmeniz gerekmez) ve **noktalar açıkken başka overlay'leri de kullanabilirsiniz.**

*Kinestop 1.x'ten mi geçiyorsunuz?* UltraHand'deki **Overlay Memory** ayarını tekrar **4MB**'a düşürebilirsiniz. Mevcut `config.ini` dosyanız korunur.

### 💾 2. Aşama: Kurulum
**Zorunlu Gereksinimler:** Uygulamanın çalışması için cihazınızda [nx-ovlloader](https://github.com/ppkantorski/nx-ovlloader) ve [Ultrahand-Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) yüklü olmak ZORUNDADIR!

**Yöntem 1: Homebrew App Store (Otomatik Kurulum)**
HB App Store'da şu an bu değiştirilmiş sürüm değil, **SertAy'ın orijinal sürümü** bulunmaktadır (yalnızca overlay, UltraHand bellek ayarının 8MB yapılmasını gerektirir).

<div align="center">
   <a href="https://hb-app.store/switch/Kinestop">
  <img src="assets/hbappstore.png" alt="HB App Store" width="250">
  <br>
  <a href="https://apps.fortheusers.org/switch/Kinestop">HB Store İndirme Linki</a>
</div>
<br>

**Yöntem 2: Manuel Kurulum**
Bu sürümün `Kinestop.zip` dosyasını indirin (ya da aşağıdaki *Kaynak koddan derleme* bölümüne göre kendiniz derleyin), bilgisayarda çıkartın ve çıkan tüm dosyaları doğrudan SD kartınızın ana dizinine sürükleyip bırakın. Dosyalar zaten doğru klasörlerdedir:
* `SD:/atmosphere/contents/4200000000001042/exefs.nsp` – noktaları çizen sistem modülü
* `SD:/atmosphere/contents/4200000000001042/toolbox.json` – sysmodule yöneticilerinin (ör. ovl-sysmodules) modülü listelemesini, başlatmasını ve durdurmasını sağlar; oradan başlatıldığında Kinestop anahtarının son durumundan bağımsız olarak noktalar her zaman görünür
* `SD:/switch/.overlays/kinestop.ovl` – ayar overlay'i

Kurulumdan sonra yeniden başlatmanız gerekmez. Ayarlarınız otomatik oluşturulan `SD:/config/kinestop/config.ini` dosyasında saklanır. Sürüm paketinde bu dosya bulunmadığı için güncelleme yaptığınızda ayarlarınız sıfırlanmaz.

### 🎮 3. Aşama: Uygulamanın Kullanımı
1. **UltraHand'i Açın:** Ekranın sol kenarından sağa kaydırarak veya **L + R + D-Pad Aşağı + Sağ Analog** kısayoluyla UltraHand'i açın.
2. **Kinestop'u Başlatın:** Listeden Kinestop'u seçin.
3. **Noktaları Açın:** **Hareket Noktalarını Aç** anahtarı arka plandaki modülü başlatır; kapattığınızda modül durur ve belleği boşaltır.
4. **Ayarlayın:** İstediğiniz rengi, yerleşim şablonunu, boyutu, saydamlığı ve hassasiyeti belirleyin. Değişiklikler anında uygulanır ve otomatik kaydedilir.
5. **Oyuna Dönün:** **(B)** tuşuna basarak menüyü kapatın. **Overlay kapandıktan sonra da noktalar çalışmaya devam eder** ve bu sırada başka overlay'leri açabilirsiniz.

**Açılışta Başlat (isteğe bağlı):** Her yeniden başlatmadan sonra overlay'i açmadan noktaların gelmesini istiyorsanız Kinestop ayarlarından **Açılışta Noktaları Başlat** seçeneğini etkinleştirin; kapatmadan önce noktaları kapatmış olsanız bile noktalar her açılışta başlar. Bu seçenek `atmosphere/contents/4200000000001042/flags/boot2.flag` dosyasını oluşturur. Açılışta başlayan sistem modülleri sınırlı bir bellek havuzunu paylaştığı için varsayılan olarak kapalıdır; konsolunuz bundan sonra açılmazsa Sorun Giderme bölümüne bakın.

**Durum satırı:** Overlay'in üst kısmı modülün ne yaptığını gösterir:
* **Çalışıyor** – noktalar çiziliyor.
* **Durduruldu** – modül çalışmıyor (noktalar kapalı).
* **Başlatılıyor...** / **Durduruluyor...** – overlay modülü başlattı ve yanıt bekliyor.
* **Çalışıyor - sensör verisi yok** – modül çalışıyor ama henüz hareket sensöründen veri gelmedi (başlatmadan hemen sonra kısa süre normaldir).
* **Hata** – bir aşama adı (VI init, Display, Layer, Layer stack, NWindow, Framebuffer, Sensör) ve `2xxx-xxxx` biçiminde bir hata kodu. O aşama düzelince kendiliğinden kaybolur. Sorun bildirirken lütfen bunları da ekleyin.
* **API uyumsuz** – overlay ile modül farklı sürümlerden; iki dosyayı da aynı sürümden kopyalayın.

Modül `SD:/config/kinestop/sysmodule.log` dosyasına bir günlük yazar. Hata bildirirken eklenecek en faydalı dosya budur.

### 🛠️ Sorun Giderme
* **Durum satırında hata var / noktalar görünmüyor:** Aşama adını ve hata kodunu not edin, noktaları kapatıp tekrar açın ve `SD:/config/kinestop/sysmodule.log` dosyasına bakın.
* **Durum "Durduruldu", "Başlatılıyor..." ya da "Başlatılamadı" olarak kalıyor:** `exefs.nsp` dosyasının `SD:/atmosphere/contents/4200000000001042/` klasöründe olduğundan ve Atmosphère'in güncel olduğundan emin olun. Çok bellek kullanan başka sistem modülleri de başlamasını engelleyebilir; bazılarını kapatmayı deneyin.
* **Açılışta Noktaları Başlat'ı açtıktan sonra konsol 2001-0132 hatasıyla açılmıyor:** Açılışta başlayan modüller sistem belleğine sığmıyor. SD kartı bilgisayara takıp `SD:/atmosphere/contents/4200000000001042/flags/boot2.flag` dosyasını silin (ya da başka modülleri kapatın). Kinestop'u overlay'den başlatmak sorunsuz çalışır.
* **Noktalar ekran görüntülerinde veya videolarda görünmüyor:** **Görüntü ve Kayıtlarda Göster** seçeneğini açın. (Temiz kayıt istiyorsanız kapalı bırakın.)
* **"API uyumsuz":** Farklı sürümlerin dosyaları karışmış; üç dosyayı da aynı sürümden yeniden kurun.
* **Kinestop'u kaldırma:** Noktaları kapatın, ardından `SD:/atmosphere/contents/4200000000001042/`, `SD:/switch/.overlays/kinestop.ovl` ve (isteğe bağlı olarak) `SD:/config/kinestop/` yollarını silin.

### 🧪 Deneme sürümü (2.0.0-test.3)
Panelin **Tanılama (test sürümü)** bölümünde test ayarları vardır (`config.ini` içinde `debug_flags` olarak saklanır): **Yanıp sönme testi** (1), **Ölçeklenmemiş test katmanı** (2, Çeyrek 1x), **Ayrıntılı günlük** (4), **Menüler açıkken noktaları göster** (8), **Küçük 2x ölçekli test katmanı** (16, Çeyrek 2x). İki katman ayarı da kapalıysa mod **Tam ekran 2x**'tir (varsayılan).

**test.3 kontrol listesi:** **Ayrıntılı günlük** açık, **Yanıp sönme testi** kapalı olsun. Her katman modunda (Tam ekran 2x, Çeyrek 1x, Çeyrek 2x) **Menüler açıkken noktaları göster** AÇIKKEN: paneli kombinasyonla gizleyip noktaların düzgün hareket ettiğini kontrol edin; Kinestop panelini açıp panelin ve noktaların görünüp görünmediğini not edin; **B**'ye basıp Ultrahand menüsünün görünüp görünmediğini not edin; varsa **Status Monitor**'ü de açın. Bir menü kaybolur ve birkaç saniyede geri gelmezse konsolu **yeniden başlatın**. **Açılışta Noktaları Başlat** kapalıysa noktalar yeniden başlatmadan sonra kapalı kalır; Kinestop'u açıp katman modunu değiştirdikten sonra noktaları yeniden açabilirsiniz. *Menüler açıkken noktaları göster* ayarı yeniden başlatmadan sonra da **açık kalır**: noktaları açmadan önce panelden kapatın. **Açılışta Noktaları Başlat** açıksa ya da panele ulaşamıyorsanız, bilgisayardan `SD:/config/kinestop/config.ini` dosyasındaki `debug_flags` değerini `4` yapın. Sonuçları ve `SD:/config/kinestop/sysmodule.log` dosyasını gönderin.

### 🔧 Kaynak koddan derleme
`switch-dev` paketiyle (libnx) birlikte [devkitPro](https://devkitpro.org/wiki/Getting_Started) ve Python 3 gerekir.
```sh
make          # sysmodule/out/kinestop.nsp ve overlay/kinestop.ovl dosyalarını derler
make dist     # SD karta kopyalanmaya hazır dosya ağacını sdcard/ içine oluşturur
make zip      # aynısı, ek olarak Kinestop.zip
make test     # bilgisayarda çalışan testler (varsa)
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

The sysmodule's IPC server code is adapted from [sys-clk](https://github.com/retronx-team/sys-clk) and keeps its original license header.
