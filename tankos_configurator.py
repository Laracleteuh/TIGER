import json
import platform
import queue
import re
import shutil
import subprocess
import threading
from datetime import datetime
from pathlib import Path
import tkinter as tk
import tkinter.font as tkfont
from tkinter import ttk

# ==============================================================================
# MÉTADONNÉES DE VERSION DU CONFIGURATEUR
# ==============================================================================
APP_NAME = "TankOS Configurator"
APP_VERSION = "1.7.1"
APP_BUILD_DATE = "2026"
TARGET_FIRMWARE = "TankOS (FreeRTOS / ATmega2560)"
PROMPT_PATH = "tankos@megaatmega2560:~$"

# ==============================================================================
# CONSTANTES MATÉRIEL (ATmega2560)
# ==============================================================================
RAM_TOTAL = 8192                 # octets de SRAM
FLASH_TOTAL = 258048             # octets de Flash programme (256 Ko, bootloader inclus)
RAM_BASE_BYTES = 2048            # estimation : noyau FreeRTOS, variables globales, tas
ROM_BASE_BYTES = 14000           # estimation : noyau Arduino, FreeRTOS, drivers communs
PIN_COLS = 8
PIN_CELL_W = 100
PIN_CELL_H = 66
PIN_GAP = 6

# Broches réservées : jamais proposées comme libres
RESERVED_PINS = {
    "D0": "UART0 RX, console USB",
    "D1": "UART0 TX, console USB",
    "D53": "SS SPI matériel",
}

# Broches de bus partagé : plusieurs modules peuvent les utiliser sans conflit
SHARED_BUS_PINS = {"D51", "D52"}

# Regroupement des broches par interface pour la vue « Interfaces de communication »
BUS_GROUPS = [
    ("SPI : MISO D50, MOSI D51, SCK D52, SS D53", ["D50", "D51", "D52", "D53"]),
    ("I2C : SDA D20, SCL D21", ["D20", "D21"]),
    ("UART1 (Serial1) : TX1 D18, RX1 D19", ["D18", "D19"]),
    ("UART0 (Serial) : RX0 D0, TX0 D1 (console USB)", ["D0", "D1"]),
    ("PWM / Sorties timers", [f"D{i}" for i in range(2, 14)] + ["D44", "D45", "D46"]),
]

# Estimation indicative de la Flash par module (octets)
ROM_ESTIMATE = {
    "traction_mdrvct": 2400,
    "rc_ht10": 900,
    "rc_ht06": 600,
    "tourelle_std": 3200,
    "batt_main": 1600,
    "batt_turret": 400,
    "current_sensor": 700,
    "temp_mcp9808": 1100,
    "temp_dht11": 900,
    "fan_pwm": 500,
    "rfid_rc522": 3800,
    "bt_hm10": 1500,
    "status_leds": 1200,
}

# ==============================================================================
# PALETTE TERMINAL VERT PHOSPHORE
# ==============================================================================
BLACK = "#000000"
BG_PANEL = "#030903"
BG_ALT = "#071407"
GREEN = "#33ff66"
GREEN_BRIGHT = "#b6ffc4"
GREEN_DIM = "#1f9a44"
GREEN_MUTED = "#2b6a3a"
GREEN_BORDER = "#135c26"
AMBER = "#ffb000"
RED = "#ff5555"
GREY = "#4d6b52"
SHARED_FILL = "#2a1f00"

# ==============================================================================
# CATALOGUE MATÉRIEL DE TANKOS
# Chaque broche est décrite par un tuple (clé, libellé court, fonction exacte).
# Source : config.h
# ==============================================================================
HARDWARE_CATALOG = [
    {
        "id": "traction_mdrvct",
        "name": "Carte traction MDRVCT002 + 2x MCP41100",
        "tag": "TRACT",
        "category": "Traction",
        "bus": "SPI + Digital",
        "macro": "HW_HAS_TRACTION_MDRVCT",
        "default": True,
        "required": True,
        "requires": [],
        "ram_words": 300,
        "driver": "src/tasks/traction/traction.cpp",
        "description": "Pilotage des deux moteurs de propulsion par relais de sens/puissance et potentiomètres numériques SPI MCP41100.",
        "pin_roles": [
            ("D22", "G AV", "Traction - relais moteur G avant (leftMotorFwdRelay)"),
            ("D23", "G AR", "Traction - relais moteur G arrière (leftMotorBckRelay)"),
            ("D24", "D AV", "Traction - relais moteur D avant (rightMotorFwdRelay)"),
            ("D25", "D AR", "Traction - relais moteur D arrière (rightMotorBckRelay)"),
            ("D26", "G ON/OFF", "Traction - marche/arrêt moteur G (leftMotorOnOffSwitch)"),
            ("D27", "D ON/OFF", "Traction - marche/arrêt moteur D (rightMotorOnOffSwitch)"),
            ("D32", "FAILSAFE", "Traction - failsafe MDRVCT (MDRVCT_FAILSAFE)"),
            ("D42", "CS1", "MCP41100 #1 - CS (CS_PIN1)"),
            ("D43", "CS2", "MCP41100 #2 - CS (CS_PIN2)"),
            ("D51", "MOSI", "SPI - MOSI (SI du MCP41100)"),
            ("D52", "SCK", "SPI - SCK (horloge MCP41100)"),
        ],
    },
    {
        "id": "rc_ht10",
        "name": "Récepteur RC principal HT-10 (Conduite)",
        "tag": "HT10",
        "category": "Radio & Comm",
        "bus": "PWM / ISR",
        "macro": "HW_HAS_RC_HT10",
        "default": True,
        "required": True,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/ISR/ISR.cpp",
        "description": "Radio de conduite principale (10 voies). Sa perte déclenche le failsafe général.",
        "pin_roles": [
            ("A8", "STEER", "HT-10 - Direction, joystick D axe X (HT10_STEER_PIN)"),
            ("A11", "GAZ", "HT-10 - Gaz, joystick G axe Y (HT10_SPEED_PIN)"),
            ("D44", "SWA", "HT-10 - SWA (SWA_PIN)"),
            ("D10", "SWB", "HT-10 - SWB (SWB_PIN)"),
            ("D11", "SWC", "HT-10 - SWC (SWC_PIN)"),
            ("D12", "SWD", "HT-10 - SWD (SWD_PIN)"),
            ("D13", "VRA", "HT-10 - VRA (VRA_PIN)"),
            ("D45", "VRB", "HT-10 - VRB (VRB_PIN)"),
        ],
    },
    {
        "id": "rc_ht06",
        "name": "Récepteur RC secondaire HT-06 (Tourelle)",
        "tag": "HT06",
        "category": "Radio & Comm",
        "bus": "PWM / ISR",
        "macro": "HW_HAS_RC_HT06",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/ISR/ISR.cpp",
        "description": "Télécommande opérateur tourelle. Sa désactivation coupe toute gestion de la tourelle.",
        "pin_roles": [
            ("A10", "TUR X", "HT-06 - Tourelle axe X (HT06_TURRET_X_PIN)"),
            ("A9", "TUR Y", "HT-06 - Tourelle axe Y (HT06_TURRET_Y_PIN)"),
            ("A13", "TIR", "HT-06 - Tir (HT06_FIRE_PIN)"),
        ],
    },
    {
        "id": "tourelle_std",
        "name": "Tourelle complète (Azimut, Trans/Élév, ESC & Tir)",
        "tag": "TOUR",
        "category": "Tourelle & Armement",
        "bus": "PWM + Digital",
        "macro": "HW_HAS_TOURELLE",
        "default": True,
        "required": False,
        "requires": ["rc_ht06"],
        "ram_words": 300,
        "driver": "src/tasks/tourelle/tourelle.cpp",
        "description": "Ensemble tourelle sur interface POGO (3 servos, 2 ESC, relais de tir et switch opérateur).",
        "pin_roles": [
            ("D2", "SERVO1", "Tourelle - servo 1 trans/élév (servo1Tourelle)"),
            ("D3", "SERVO2", "Tourelle - servo 2 trans/élév (servo2Tourelle)"),
            ("D4", "ESC1", "Tourelle - ESC 1 (esc1Tourelle)"),
            ("D5", "ESC2", "Tourelle - ESC 2 (esc2Tourelle)"),
            ("D6", "AZIMUT", "Tourelle - servo azimut (servoAzimutTourelle)"),
            ("D31", "SWITCH", "Tourelle - switch opérateur (switchPIN)"),
            ("D38", "TIR", "Tourelle - sortie de tir (TIR_PIN)"),
        ],
    },
    {
        "id": "batt_main",
        "name": "Pont diviseur Batterie Principale (6S LiPo)",
        "tag": "BAT6S",
        "category": "Énergie & Sécurité",
        "bus": "Analogique (ADC)",
        "macro": "HW_HAS_MAIN_BATTERY",
        "default": True,
        "required": True,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/batterie/batterie.cpp",
        "description": "Mesure de tension 6S avec table LUT en Flash et bridage automatique.",
        "pin_roles": [
            ("A1", "BAT 6S", "Batterie châssis 6S - pont diviseur (MAIN_BATTERY_PIN)"),
        ],
    },
    {
        "id": "batt_turret",
        "name": "Pont diviseur Batterie Tourelle (3S LiPo)",
        "tag": "BAT3S",
        "category": "Énergie & Sécurité",
        "bus": "Analogique (ADC)",
        "macro": "HW_HAS_TURRET_BATTERY",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/batterie/batterie.cpp",
        "description": "Surveillance dédiée de la batterie tourelle. À décocher si absente.",
        "pin_roles": [
            ("A2", "BAT 3S", "Batterie tourelle 3S - pont diviseur (TURRET_BATTERY_PIN)"),
        ],
    },
    {
        "id": "current_sensor",
        "name": "Capteur de courant châssis (20 mV/A)",
        "tag": "AMP",
        "category": "Énergie & Sécurité",
        "bus": "Analogique (ADC)",
        "macro": "HW_HAS_CURRENT_SENSOR",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/batterie/batterie.cpp",
        "description": "Mesure l'intensité totale, la tare au démarrage et le bridage au-delà de 20 A.",
        "pin_roles": [
            ("A0", "AMP", "Capteur de courant châssis 20 mV/A (CURRENT_SENSOR_PIN)"),
        ],
    },
    {
        "id": "temp_mcp9808",
        "name": "Capteur de température haute précision MCP9808",
        "tag": "MCP",
        "category": "Thermique",
        "bus": "I2C (0x18)",
        "macro": "HW_HAS_MCP9808",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/hardware_drivers/tankos_MCP9808_Sensor/",
        "description": "Capteur numérique I2C précis (±0,5 °C).",
        "pin_roles": [
            ("D20", "SDA", "MCP9808 - I2C SDA, adresse 0x18"),
            ("D21", "SCL", "MCP9808 - I2C SCL, adresse 0x18"),
        ],
    },
    {
        "id": "temp_dht11",
        "name": "Capteur température/humidité DHT11",
        "tag": "DHT",
        "category": "Thermique",
        "bus": "1-Wire propriétaire",
        "macro": "HW_HAS_DHT11",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/hardware_drivers/tankos_DHT_Sensor/",
        "description": "Capteur secondaire. Si les deux capteurs thermiques sont décochés, TaskTemperature est supprimée.",
        "pin_roles": [
            ("D30", "DATA", "DHT11 - données (DHT_PIN)"),
        ],
    },
    {
        "id": "fan_pwm",
        "name": "Ventilation refroidissement (PWM Timer 2)",
        "tag": "FAN",
        "category": "Thermique",
        "bus": "PWM Registres AVR",
        "macro": "HW_HAS_FAN_PWM",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/systeme/systeme.cpp",
        "description": "Pilotage PWM haute fréquence sur Timer 2 et commandes fanctrl/fanspd/fanrange.",
        "pin_roles": [
            ("D9", "PWM FAN", "Ventilation - PWM Timer 2 OCR2B (fanPWMPin)"),
        ],
    },
    {
        "id": "rfid_rc522",
        "name": "Lecteur badge RFID Admin MFRC522",
        "tag": "RFID",
        "category": "Admin & Sécurité",
        "bus": "SPI",
        "macro": "HW_HAS_RFID_RC522",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 300,
        "driver": "src/tasks/admin/admin.cpp",
        "description": "Déverrouillage par badge NFC. Si décoché, TaskAdmin est supprimée et les commandes restent accessibles.",
        "pin_roles": [
            ("D41", "RC SS", "RC522 - SDA / SS (RFID_SS_PIN)"),
            ("D40", "RC RST", "RC522 - RST (RFID_RST_PIN)"),
            ("D50", "MISO", "SPI - MISO (sortie RC522)"),
            ("D51", "MOSI", "SPI - MOSI (entrée RC522)"),
            ("D52", "SCK", "SPI - SCK (horloge RC522)"),
        ],
    },
    {
        "id": "bt_hm10",
        "name": "Module Bluetooth BLE HM-10",
        "tag": "HM10",
        "category": "Radio & Comm",
        "bus": "UART (Serial1)",
        "macro": "HW_HAS_BT_HM10",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/tasks/communication/communication.cpp",
        "description": "Télémétrie sans fil et console Bluetooth (la console USB Serial reste active même si décoché).",
        "pin_roles": [
            ("D18", "TX1", "HM-10 - UART1 TX, Serial1 (BT_SERIAL)"),
            ("D19", "RX1", "HM-10 - UART1 RX, Serial1 (BT_SERIAL)"),
            ("D37", "BRK", "HM-10 - BRK (BT_BRK_PIN)"),
        ],
    },
    {
        "id": "status_leds",
        "name": "Panneau LEDs de statut & LED RGB",
        "tag": "LED",
        "category": "Interface & Diagnostic",
        "bus": "PWM + Digital",
        "macro": "HW_HAS_STATUS_LEDS",
        "default": True,
        "required": False,
        "requires": [],
        "ram_words": 0,
        "driver": "src/hardware_drivers/tankos_leds/",
        "description": "Signalisation visuelle de l'état système et des alertes.",
        "pin_roles": [
            ("D49", "LED R", "LED RGB - rouge (RStatusLED)"),
            ("D47", "LED G", "LED RGB - vert (GStatusLED)"),
            ("D46", "LED B", "LED RGB - bleu (BStatusLED)"),
            ("D48", "LED TUR", "LED état contrôle tourelle (TurretControlStatusLED)"),
            ("D39", "LED ADM", "LED admin (adminStatusLED)"),
            ("D35", "LED BAT", "LED batterie faible (lowBattStatusLED)"),
            ("D36", "LED AMP", "LED surintensité (overAmpsStatusLED)"),
            ("D34", "LED COM", "LED communication (commStatusLED)"),
        ],
    },
]

# Champs dérivés, calculés une fois à partir de pin_roles
for _item in HARDWARE_CATALOG:
    _item["pin_keys"] = list(dict.fromkeys(k for k, _s, _l in _item["pin_roles"]))
    _item["pins_str"] = ", ".join(_item["pin_keys"])
    _item["pins_compact"] = "   ".join(f"{k} {s}" for k, s, _l in _item["pin_roles"])

PAGE_KEYS = ["inventory", "selection", "pins", "resources", "build"]
PAGE_LABELS = {
    "inventory": "[F1]  Composants du projet",
    "selection": "[F2]  Mon matériel installé",
    "pins": "[F3]  Broches de la carte",
    "resources": "[F4]  Ressources",
    "build": "[F5]  Génération et compilation",
}
PAGE_CMDS = {
    "inventory": "dir modules",
    "selection": "config --hardware",
    "pins": "pinmap --show",
    "resources": "usage --report",
    "build": "build --target megaatmega2560",
}

VIEW_FULL = "Vue carte entière"
VIEW_DIGITAL = "Entrées Digitales"
VIEW_ANALOG = "Entrées Analogiques"
VIEW_COMM = "Interfaces de communication (SPI, UART, I2C, etc.)"
PIN_VIEWS = [VIEW_FULL, VIEW_DIGITAL, VIEW_ANALOG, VIEW_COMM]

DIGITAL_KEYS = [f"D{i}" for i in range(54)]
ANALOG_KEYS = [f"A{i}" for i in range(16)]

BUTTON_KIND_COLORS = {
    "normal": GREEN,
    "primary": GREEN_BRIGHT,
    "warning": AMBER,
    "danger": RED,
}

# Format de sortie PlatformIO : « RAM: [...] 12.3% (used 1245 bytes from 8192 bytes) »
RAM_RE = re.compile(r"RAM:.*?used\s+(\d+)\s+bytes from\s+(\d+)\s+bytes")
FLASH_RE = re.compile(r"Flash:.*?used\s+(\d+)\s+bytes from\s+(\d+)\s+bytes")


# ==============================================================================
# UTILITAIRES
# ==============================================================================
def _pick_font(candidates: list[str], fallback: str) -> str:
    available = set(tkfont.families())
    for name in candidates:
        if name in available:
            return name
    return fallback


def _chunks(items: list, size: int):
    for start in range(0, len(items), size):
        yield items[start:start + size]


def _fit(text: str, width: int = 14) -> str:
    """Tronque un libellé pour qu'il tienne dans une cellule."""
    return text if len(text) <= width else text[: width - 2] + ".."


def _set_text(widget: tk.Text, content: str):
    widget.config(state="normal")
    widget.delete("1.0", "end")
    widget.insert("1.0", content)
    widget.config(state="disabled")


def attach_mousewheel(widget, target):
    """Active la molette sur 'target' tant que le pointeur est sur 'widget'."""

    def on_wheel(event):
        if event.num == 4:
            target.yview_scroll(-1, "units")
        elif event.num == 5:
            target.yview_scroll(1, "units")
        else:
            target.yview_scroll(int(-1 * (event.delta / 120)), "units")

    def bind_all(_e):
        widget.bind_all("<MouseWheel>", on_wheel)
        widget.bind_all("<Button-4>", on_wheel)
        widget.bind_all("<Button-5>", on_wheel)

    def unbind_all(_e):
        widget.unbind_all("<MouseWheel>")
        widget.unbind_all("<Button-4>")
        widget.unbind_all("<Button-5>")

    widget.bind("<Enter>", bind_all)
    widget.bind("<Leave>", unbind_all)


# ==============================================================================
# WIDGETS PERSONNALISÉS
# ==============================================================================
class CmdScrollbar(tk.Canvas):
    """Barre de défilement dans le style terminal (glissière verte sur fond noir)."""

    WIDTH = 12

    def __init__(self, master, command, parent_bg: str = BLACK):
        super().__init__(master, width=self.WIDTH, bg=parent_bg, highlightthickness=0, bd=0, cursor="hand2")
        self._sb_cmd = command
        self._sb_lo = 0.0
        self._sb_hi = 1.0
        self._sb_origin = None
        self.bind("<Configure>", lambda _e: self._draw())
        self.bind("<Button-1>", self._on_press)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_release)

    def set(self, lo, hi):
        """Reçoit la position courante du widget défilé (appelé par yscrollcommand)."""
        self._sb_lo = float(lo)
        self._sb_hi = float(hi)
        self._draw()

    def _draw(self):
        self.delete("all")
        w, h = self.WIDTH, self.winfo_height()
        if h < 4:
            return
        self.create_rectangle(0, 0, w - 1, h - 1, fill=BLACK, outline=GREEN_BORDER)
        if self._sb_hi - self._sb_lo >= 0.999:
            return
        top = self._sb_lo * h + 2
        bottom = max(self._sb_hi * h - 2, top + 16)
        self.create_rectangle(2, top, w - 3, bottom, fill=GREEN_DIM, outline=GREEN)

    def _on_press(self, event):
        h = max(self.winfo_height(), 1)
        frac = event.y / h
        if self._sb_lo <= frac <= self._sb_hi:
            self._sb_origin = (event.y, self._sb_lo)
        else:
            span = self._sb_hi - self._sb_lo
            self._sb_cmd("moveto", max(0.0, frac - span / 2))

    def _on_drag(self, event):
        if self._sb_origin is None:
            return
        h = max(self.winfo_height(), 1)
        start_y, start_lo = self._sb_origin
        delta = (event.y - start_y) / h
        self._sb_cmd("moveto", max(0.0, start_lo + delta))

    def _on_release(self, _event):
        self._sb_origin = None


def make_text(parent, bg: str, fg: str, font, border: str = GREEN_BORDER, height: int = 10):
    """Zone de texte avec barre de défilement stylée. Retourne (cadre, widget Text)."""
    frame = tk.Frame(parent, bg=bg, highlightthickness=1, highlightbackground=border)
    text = tk.Text(
        frame,
        bg=bg,
        fg=fg,
        font=font,
        insertbackground=fg,
        relief="flat",
        bd=0,
        padx=12,
        pady=10,
        highlightthickness=0,
        wrap="none",
        state="disabled",
        width=48,
        height=height,
    )
    sb = CmdScrollbar(frame, command=text.yview, parent_bg=bg)
    text.configure(yscrollcommand=sb.set)
    sb.pack(side="right", fill="y", padx=2, pady=2)
    text.pack(side="left", fill="both", expand=True)
    return frame, text


class CmdButton(tk.Label):
    """Bouton texte entre crochets, qui s'inverse au survol comme une sélection de console."""

    def __init__(self, master, text: str, command=None, kind: str = "normal", font=None, parent_bg: str = BLACK):
        self._cb_text = text
        self._cb_cmd = command
        self._cb_kind = kind
        self._cb_parent = parent_bg
        self._cb_enabled = True
        self._cb_hover = False
        super().__init__(
            master,
            text=f"[ {text} ]",
            font=font,
            bg=parent_bg,
            fg=BUTTON_KIND_COLORS[kind],
            cursor="hand2",
            padx=6,
            pady=4,
        )
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<ButtonRelease-1>", self._on_click)
        self._paint()

    def _paint(self):
        accent = BUTTON_KIND_COLORS[self._cb_kind]
        if not self._cb_enabled:
            bg, fg = self._cb_parent, GREY
        elif self._cb_hover:
            bg, fg = accent, BLACK
        else:
            bg, fg = self._cb_parent, accent
        self.configure(text=f"[ {self._cb_text} ]", bg=bg, fg=fg)

    def _on_enter(self, _event):
        if self._cb_enabled:
            self._cb_hover = True
            self._paint()

    def _on_leave(self, _event):
        self._cb_hover = False
        self._paint()

    def _on_click(self, _event):
        if self._cb_enabled and self._cb_cmd:
            self._cb_cmd()

    def set_text(self, text: str):
        self._cb_text = text
        self._paint()

    def set_enabled(self, flag: bool):
        self._cb_enabled = flag
        self._cb_hover = False
        self.configure(cursor="hand2" if flag else "arrow")
        self._paint()


class CmdToggle(tk.Label):
    """Case à cocher style console : [ ON ] / [OFF ]."""

    def __init__(self, master, variable: tk.BooleanVar, font=None, command=None, parent_bg: str = BG_PANEL):
        super().__init__(master, font=font, bg=parent_bg, fg=GREEN, cursor="hand2", padx=4, pady=2)
        self._ct_var = variable
        self._ct_cmd = command
        self._ct_parent = parent_bg
        self._ct_enabled = True
        self._ct_hover = False
        self._ct_var.trace_add("write", lambda *_: self._paint())
        self.bind("<Button-1>", self._on_click)
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self._paint()

    def _paint(self):
        on = bool(self._ct_var.get())
        text = "[ ON ]" if on else "[OFF ]"
        if not self._ct_enabled:
            fg = GREY
        else:
            fg = GREEN_BRIGHT if on else GREEN_DIM
        bg = BG_ALT if (self._ct_hover and self._ct_enabled) else self._ct_parent
        self.configure(text=text, fg=fg, bg=bg)

    def _on_enter(self, _event):
        self._ct_hover = True
        self._paint()

    def _on_leave(self, _event):
        self._ct_hover = False
        self._paint()

    def _on_click(self, _event):
        if not self._ct_enabled:
            return
        self._ct_var.set(not self._ct_var.get())
        if self._ct_cmd:
            self._ct_cmd()

    def set_enabled(self, flag: bool):
        self._ct_enabled = flag
        self.configure(cursor="hand2" if flag else "arrow")
        self._paint()


class CmdProgress(tk.Label):
    """Barre de progression ASCII : [#####.....] 320 / 1600 o."""

    WIDTH = 40

    def __init__(self, master, maximum: int, font=None, parent_bg: str = BG_PANEL, unit: str = "o"):
        super().__init__(master, font=font, bg=parent_bg, fg=GREEN, anchor="w", justify="left")
        self._cp_max = maximum
        self._cp_unit = unit
        self.set_value(0)

    def set_value(self, value: int, maximum: int | None = None):
        if maximum is not None:
            self._cp_max = maximum
        value = max(0, min(value, self._cp_max))
        filled = round(self.WIDTH * value / self._cp_max) if self._cp_max else 0
        bar = "#" * filled + "." * (self.WIDTH - filled)
        ratio = value / self._cp_max if self._cp_max else 0
        self.configure(
            text=f"[{bar}] {value} / {self._cp_max} {self._cp_unit}",
            fg=AMBER if ratio > 0.85 else GREEN,
        )


# ==============================================================================
# APPLICATION
# ==============================================================================
class TankOSConfigurator(tk.Tk):
    PIN_IDLE_TEXT = "Survole une broche pour afficher sa fonction exacte."

    def __init__(self):
        super().__init__()
        self.mono = _pick_font(
            ["Consolas", "Lucida Console", "Menlo", "DejaVu Sans Mono", "Courier New"],
            "Courier",
        )

        self.title(f"{APP_NAME} v{APP_VERSION}")
        self.geometry("1220x800")
        self.minsize(1040, 700)
        self.configure(bg=BLACK)

        self.project_dir = Path(__file__).resolve().parent
        self.config_json_path = self.project_dir / "user_hardware.json"
        self.header_output_path = self.project_dir / "include" / "hardware_config.h"

        self.hw_vars: dict[str, tk.BooleanVar] = {}
        self.switches: dict[str, CmdToggle] = {}
        self.catalog_by_id = {item["id"]: item for item in HARDWARE_CATALOG}
        self.pages: dict[str, tk.Frame] = {}
        self.nav_items: dict[str, tk.Label] = {}
        self.measured: dict[str, tuple[int, int]] = {}
        self._active_page = None
        self._busy = False
        self._ui_queue: queue.Queue = queue.Queue()

        self._setup_styles()
        self._init_variables()
        self._load_saved_selection()
        self._build_ui()
        self._enforce_dependencies()
        self._update_select_all_button()
        self._refresh_preview_and_status()
        self._bind_keys()
        self._show_page("inventory")
        self._blink_cursor()
        self.after(50, self._poll_queue)

    # ==========================================================================
    # THÈME TTK
    # ==========================================================================
    def _setup_styles(self):
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")

        style.configure(".", background=BLACK, foreground=GREEN, font=(self.mono, 10))

        style.configure(
            "Cmd.TCombobox",
            fieldbackground=BG_PANEL,
            background=BG_PANEL,
            foreground=GREEN,
            arrowcolor=GREEN,
            bordercolor=GREEN_BORDER,
            lightcolor=BG_PANEL,
            darkcolor=BG_PANEL,
            padding=(8, 5),
            font=(self.mono, 10),
        )
        style.map(
            "Cmd.TCombobox",
            fieldbackground=[("readonly", BG_PANEL)],
            foreground=[("readonly", GREEN)],
            bordercolor=[("focus", GREEN)],
            selectbackground=[("readonly", GREEN)],
            selectforeground=[("readonly", BLACK)],
        )
        self.option_add("*TCombobox*Listbox.background", BG_PANEL)
        self.option_add("*TCombobox*Listbox.foreground", GREEN)
        self.option_add("*TCombobox*Listbox.selectBackground", GREEN)
        self.option_add("*TCombobox*Listbox.selectForeground", BLACK)
        self.option_add("*TCombobox*Listbox.font", (self.mono, 10))

        style.configure(
            "Cmd.Treeview",
            background=BG_PANEL,
            foreground=GREEN,
            fieldbackground=BG_PANEL,
            borderwidth=0,
            rowheight=28,
            font=(self.mono, 10),
        )
        style.configure(
            "Cmd.Treeview.Heading",
            background=GREEN_BORDER,
            foreground=GREEN_BRIGHT,
            borderwidth=0,
            relief="flat",
            padding=(10, 8),
            font=(self.mono, 10, "bold"),
        )
        style.map(
            "Cmd.Treeview",
            background=[("selected", GREEN)],
            foreground=[("selected", BLACK)],
        )
        style.map("Cmd.Treeview.Heading", background=[("active", GREEN_DIM)])

    def _init_variables(self):
        for item in HARDWARE_CATALOG:
            self.hw_vars[item["id"]] = tk.BooleanVar(value=item["default"])

    def _load_saved_selection(self):
        if not self.config_json_path.exists():
            return
        try:
            data = json.loads(self.config_json_path.read_text(encoding="utf-8"))
            for hw_id, enabled in data.items():
                if hw_id in self.hw_vars and not self.catalog_by_id[hw_id]["required"]:
                    self.hw_vars[hw_id].set(bool(enabled))
        except Exception:
            pass

    def _save_selection(self):
        data = {hw_id: var.get() for hw_id, var in self.hw_vars.items()}
        self.config_json_path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")

    # ==========================================================================
    # STRUCTURE : BARRE DE TITRE, MENU, CORPS, BARRE D'ÉTAT
    # ==========================================================================
    def _build_ui(self):
        self._build_titlebar()
        self._build_statusbar()

        body = tk.Frame(self, bg=BLACK)
        body.pack(fill="both", expand=True)

        sidebar = tk.Frame(body, bg=BG_PANEL, width=280, highlightthickness=1, highlightbackground=GREEN_BORDER)
        sidebar.pack(side="left", fill="y")
        sidebar.pack_propagate(False)
        self._build_sidebar(sidebar)

        main = tk.Frame(body, bg=BLACK)
        main.pack(side="left", fill="both", expand=True)
        self._build_prompt(main)

        content = tk.Frame(main, bg=BLACK)
        content.pack(fill="both", expand=True, padx=24, pady=(6, 12))

        for key in PAGE_KEYS:
            self.pages[key] = tk.Frame(content, bg=BLACK)

        self._build_inventory_page(self.pages["inventory"])
        self._build_selection_page(self.pages["selection"])
        self._build_pins_page(self.pages["pins"])
        self._build_resources_page(self.pages["resources"])
        self._build_build_page(self.pages["build"])

    def _build_titlebar(self):
        bar = tk.Frame(self, bg=GREEN_BORDER)
        bar.pack(fill="x")

        tk.Label(
            bar,
            text=f" {APP_NAME} v{APP_VERSION}  -  {TARGET_FIRMWARE}",
            bg=GREEN_BORDER,
            fg=GREEN_BRIGHT,
            font=(self.mono, 10, "bold"),
            anchor="w",
        ).pack(side="left", pady=3)

        close_lbl = tk.Label(bar, text=" X ", bg=GREEN_BORDER, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), cursor="hand2")
        close_lbl.pack(side="right")
        close_lbl.bind("<Button-1>", lambda _e: self.destroy())
        close_lbl.bind("<Enter>", lambda _e: close_lbl.configure(bg=RED, fg=BLACK))
        close_lbl.bind("<Leave>", lambda _e: close_lbl.configure(bg=GREEN_BORDER, fg=GREEN_BRIGHT))

        about_lbl = tk.Label(bar, text=" ? ", bg=GREEN_BORDER, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), cursor="hand2")
        about_lbl.pack(side="right")
        about_lbl.bind("<Button-1>", lambda _e: self._show_version_dialog())
        about_lbl.bind("<Enter>", lambda _e: about_lbl.configure(bg=GREEN, fg=BLACK))
        about_lbl.bind("<Leave>", lambda _e: about_lbl.configure(bg=GREEN_BORDER, fg=GREEN_BRIGHT))

    def _build_statusbar(self):
        self.status_bar = tk.Label(
            self,
            text="",
            bg=GREEN_DIM,
            fg=BLACK,
            font=(self.mono, 9, "bold"),
            anchor="w",
            padx=10,
            pady=3,
        )
        self.status_bar.pack(side="bottom", fill="x")

    def _build_sidebar(self, sidebar: tk.Frame):
        tk.Label(
            sidebar,
            text="MENU PRINCIPAL",
            bg=BG_PANEL,
            fg=GREEN_DIM,
            font=(self.mono, 9, "bold"),
            anchor="w",
        ).pack(fill="x", padx=18, pady=(20, 10))

        for key in PAGE_KEYS:
            self._make_nav_item(sidebar, key, PAGE_LABELS[key])

        footer = tk.Frame(sidebar, bg=BG_PANEL)
        footer.pack(side="bottom", fill="x", padx=18, pady=16)
        tk.Label(
            footer,
            text="Arduino Mega 2560\nATmega2560 - 8 Ko SRAM - 256 Ko Flash",
            bg=BG_PANEL,
            fg=GREEN_MUTED,
            font=(self.mono, 9),
            justify="left",
            anchor="w",
        ).pack(anchor="w")
        tk.Label(
            footer,
            text="include/hardware_config.h",
            bg=BG_PANEL,
            fg=GREEN_MUTED,
            font=(self.mono, 9),
            justify="left",
            anchor="w",
            wraplength=240,
        ).pack(anchor="w", pady=(6, 0))

    def _make_nav_item(self, parent: tk.Frame, key: str, label: str):
        lbl = tk.Label(
            parent,
            text=label,
            bg=BG_PANEL,
            fg=GREEN,
            font=(self.mono, 10),
            anchor="w",
            padx=18,
            pady=10,
            cursor="hand2",
        )
        lbl.pack(fill="x")
        lbl.bind("<Button-1>", lambda _e, k=key: self._show_page(k))
        lbl.bind("<Enter>", lambda _e, k=key: self._nav_hover(k, True))
        lbl.bind("<Leave>", lambda _e, k=key: self._nav_hover(k, False))
        self.nav_items[key] = lbl

    def _nav_hover(self, key: str, on: bool):
        if key == self._active_page:
            return
        self.nav_items[key].configure(bg=BG_ALT if on else BG_PANEL, fg=GREEN_BRIGHT if on else GREEN)

    def _show_page(self, key: str):
        self._active_page = key
        for frame in self.pages.values():
            frame.pack_forget()
        self.pages[key].pack(fill="both", expand=True)
        self.prompt_cmd.config(text=f"{PROMPT_PATH} {PAGE_CMDS[key]}")

        for name, lbl in self.nav_items.items():
            if name == key:
                lbl.configure(bg=GREEN, fg=BLACK)
            else:
                lbl.configure(bg=BG_PANEL, fg=GREEN)

    def _bind_keys(self):
        self.bind("<F1>", lambda _e: self._show_page("inventory"))
        self.bind("<F2>", lambda _e: self._show_page("selection"))
        self.bind("<F3>", lambda _e: self._show_page("pins"))
        self.bind("<F4>", lambda _e: self._show_page("resources"))
        self.bind("<F5>", lambda _e: self._show_page("build"))
        self.bind("<F6>", lambda _e: self._generate_header_only())
        self.bind("<F7>", lambda _e: self._run_platformio(upload=False))
        self.bind("<F8>", lambda _e: self._run_platformio(upload=True))

    def _build_prompt(self, parent: tk.Frame):
        line = tk.Frame(parent, bg=BLACK)
        line.pack(fill="x", padx=24, pady=(22, 0))
        self.prompt_cmd = tk.Label(line, text="", bg=BLACK, fg=GREEN, font=(self.mono, 13, "bold"), anchor="w")
        self.prompt_cmd.pack(side="left")
        self.cursor_label = tk.Label(line, text="_", bg=BLACK, fg=GREEN, font=(self.mono, 13, "bold"))
        self.cursor_label.pack(side="left")

        tk.Frame(parent, bg=GREEN_BORDER, height=1).pack(fill="x", padx=24, pady=(12, 0))

    def _blink_cursor(self):
        current = self.cursor_label.cget("fg")
        self.cursor_label.configure(fg=BLACK if current == GREEN else GREEN)
        self.after(500, self._blink_cursor)

    def _make_box(self, parent, padx: int = 16, pady: int = 14) -> tk.Frame:
        return tk.Frame(
            parent,
            bg=BG_PANEL,
            highlightthickness=1,
            highlightbackground=GREEN_BORDER,
            padx=padx,
            pady=pady,
        )

    def _make_stat_box(self, parent, caption: str, value: str, color: str):
        box = self._make_box(parent, padx=18, pady=12)
        tk.Label(
            box, text=caption.upper(), bg=BG_PANEL, fg=GREEN_DIM, font=(self.mono, 9, "bold"), anchor="w"
        ).pack(anchor="w")
        value_lbl = tk.Label(box, text=value, bg=BG_PANEL, fg=color, font=(self.mono, 24, "bold"), anchor="w")
        value_lbl.pack(anchor="w", pady=(4, 0))
        return box, value_lbl

    # --------------------------------------------------------------------------
    # PAGE 1 : INVENTAIRE MATÉRIEL
    # --------------------------------------------------------------------------
    def _build_inventory_page(self, parent: tk.Frame):
        stats = tk.Frame(parent, bg=BLACK)
        stats.pack(fill="x", pady=(0, 16))
        self.stat_labels: dict[str, tk.Label] = {}

        specs = [
            ("total", "Modules", str(len(HARDWARE_CATALOG)), GREEN_BRIGHT),
            ("active", "Actifs", "0", GREEN),
            ("required", "Requis", str(sum(1 for i in HARDWARE_CATALOG if i["required"])), AMBER),
            ("pins", "Broches", "0", GREEN),
        ]
        for col, (key, caption, value, color) in enumerate(specs):
            box, value_lbl = self._make_stat_box(stats, caption, value, color)
            box.grid(row=0, column=col, sticky="nsew", padx=(0 if col == 0 else 10, 0))
            stats.grid_columnconfigure(col, weight=1, uniform="stat")
            self.stat_labels[key] = value_lbl

        toolbar = tk.Frame(parent, bg=BLACK)
        toolbar.pack(fill="x", pady=(0, 12))
        tk.Label(toolbar, text="filtre >", bg=BLACK, fg=GREEN_DIM, font=(self.mono, 10)).pack(side="left")

        categories = ["Toutes"] + sorted({item["category"] for item in HARDWARE_CATALOG})
        self.filter_var = tk.StringVar(value="Toutes")
        combo = ttk.Combobox(
            toolbar,
            textvariable=self.filter_var,
            values=categories,
            state="readonly",
            width=28,
            style="Cmd.TCombobox",
        )
        combo.pack(side="left", padx=10)
        combo.bind("<<ComboboxSelected>>", lambda _e: self._populate_inventory_tree())

        self.inventory_count_label = tk.Label(toolbar, text="", bg=BLACK, fg=GREEN_DIM, font=(self.mono, 10))
        self.inventory_count_label.pack(side="right")

        tree_frame = tk.Frame(parent, bg=BLACK, highlightthickness=1, highlightbackground=GREEN_BORDER)
        tree_frame.pack(fill="x")

        columns = ("name", "category", "bus", "pins", "macro")
        self.tree = ttk.Treeview(tree_frame, columns=columns, show="headings", height=8, style="Cmd.Treeview")
        self.tree.heading("name", text="COMPOSANT", anchor="w")
        self.tree.heading("category", text="SOUS-SYSTÈME", anchor="w")
        self.tree.heading("bus", text="BUS", anchor="w")
        self.tree.heading("pins", text="BROCHES", anchor="w")
        self.tree.heading("macro", text="MACRO C++", anchor="w")

        self.tree.column("name", width=290, anchor="w")
        self.tree.column("category", width=160, anchor="w")
        self.tree.column("bus", width=130, anchor="w")
        self.tree.column("pins", width=230, anchor="w")
        self.tree.column("macro", width=200, anchor="w")

        self.tree.tag_configure("odd", background=BG_PANEL)
        self.tree.tag_configure("even", background=BG_ALT)

        tree_scroll = CmdScrollbar(tree_frame, command=self.tree.yview, parent_bg=BLACK)
        self.tree.configure(yscrollcommand=tree_scroll.set)
        tree_scroll.pack(side="right", fill="y", padx=2, pady=2)
        self.tree.pack(side="left", fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", self._on_inventory_select)

        detail = self._make_box(parent, padx=20, pady=12)
        detail.pack(fill="both", expand=True, pady=(16, 0))
        tk.Label(
            detail, text="> DÉTAILS DU MODULE", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
        ).pack(anchor="w", pady=(0, 6))
        self.detail_label = tk.Label(
            detail,
            text="Sélectionne une ligne du tableau pour afficher son pilote, son statut et la fonction de chaque broche.",
            bg=BG_PANEL,
            fg=GREEN,
            wraplength=980,
            justify="left",
            anchor="w",
            font=(self.mono, 10),
        )
        self.detail_label.pack(anchor="w", fill="x", pady=(0, 8))
        roles_frame, self.detail_roles = make_text(detail, BLACK, GREEN, (self.mono, 9), height=7)
        roles_frame.pack(fill="both", expand=True)
        self._populate_inventory_tree()

    def _populate_inventory_tree(self):
        for row in self.tree.get_children():
            self.tree.delete(row)
        selected_cat = self.filter_var.get()
        idx = 0
        for item in HARDWARE_CATALOG:
            if selected_cat != "Toutes" and item["category"] != selected_cat:
                continue
            tag = "even" if idx % 2 == 0 else "odd"
            self.tree.insert(
                "",
                "end",
                iid=item["id"],
                values=(item["name"], item["category"], item["bus"], item["pins_str"], item["macro"]),
                tags=(tag,),
            )
            idx += 1
        self.inventory_count_label.config(text=f"{idx} entrée(s)")

    def _on_inventory_select(self, _event):
        sel = self.tree.selection()
        if not sel:
            return
        item = self.catalog_by_id[sel[0]]
        req_txt = "noyau obligatoire" if item["required"] else "optionnel"
        deps = ", ".join(self.catalog_by_id[d]["name"] for d in item["requires"]) or "aucune"
        self.detail_label.config(
            text=(
                f"pilote     : {item['driver']}\n"
                f"statut     : {req_txt}\n"
                f"dépendance : {deps}\n\n"
                f"{item['description']}"
            )
        )
        roles = "\n".join(f"{k:<5} {s:<11} {l}" for k, s, l in item["pin_roles"])
        _set_text(self.detail_roles, roles)

    # --------------------------------------------------------------------------
    # PAGE 2 : SÉLECTION DU MATÉRIEL INSTALLÉ
    # --------------------------------------------------------------------------
    def _build_selection_page(self, parent: tk.Frame):
        top = tk.Frame(parent, bg=BLACK)
        top.pack(fill="x", pady=(0, 14))
        tk.Label(
            top,
            text="Coche uniquement les modules câblés sur la carte Mega 2560.",
            bg=BLACK,
            fg=GREEN,
            font=(self.mono, 10),
        ).pack(side="left")
        self.btn_toggle_all = CmdButton(
            top,
            text="Tout désélectionner",
            command=self._toggle_select_all,
            kind="normal",
            font=(self.mono, 10, "bold"),
            parent_bg=BLACK,
        )
        self.btn_toggle_all.pack(side="right")

        summary = self._make_box(parent, padx=20, pady=14)
        summary.pack(side="bottom", fill="x", pady=(14, 0))
        tk.Label(
            summary, text="> IMPACT FREERTOS ET SRAM", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
        ).pack(anchor="w")
        self.status_label = tk.Label(
            summary, text="", bg=BG_PANEL, fg=GREEN, justify="left", anchor="w", font=(self.mono, 10)
        )
        self.status_label.pack(fill="x", pady=(8, 8))
        self.ram_progress = CmdProgress(summary, maximum=1600, font=(self.mono, 10), parent_bg=BG_PANEL)
        self.ram_progress.pack(anchor="w")
        self.ram_badge_label = tk.Label(
            summary, text="", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
        )
        self.ram_badge_label.pack(anchor="w", pady=(6, 0))

        container = tk.Frame(parent, bg=BLACK)
        container.pack(fill="both", expand=True)

        canvas = tk.Canvas(container, bg=BLACK, highlightthickness=0, bd=0)
        scrollbar = CmdScrollbar(container, command=canvas.yview, parent_bg=BLACK)
        scroll_frame = tk.Frame(canvas, bg=BLACK)

        window_id = canvas.create_window((0, 0), window=scroll_frame, anchor="nw")
        scroll_frame.bind("<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(window_id, width=e.width))
        canvas.configure(yscrollcommand=scrollbar.set)

        scrollbar.pack(side="right", fill="y", padx=(6, 0))
        canvas.pack(side="left", fill="both", expand=True)
        attach_mousewheel(canvas, canvas)

        by_cat: dict[str, list[dict]] = {}
        for item in HARDWARE_CATALOG:
            by_cat.setdefault(item["category"], []).append(item)

        scroll_frame.grid_columnconfigure(0, weight=1, uniform="cols")
        scroll_frame.grid_columnconfigure(1, weight=1, uniform="cols")

        for index, (cat, items) in enumerate(by_cat.items()):
            col = index % 2
            row = index // 2
            card = self._make_box(scroll_frame, padx=18, pady=12)
            card.grid(row=row, column=col, sticky="nsew", padx=(0, 8) if col == 0 else (8, 0), pady=(0, 14))
            tk.Label(
                card, text=f"[ {cat.upper()} ]", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
            ).pack(anchor="w", pady=(0, 4))
            for item in items:
                self._build_hw_row(card, item)

    def _build_hw_row(self, card: tk.Frame, item: dict):
        row = tk.Frame(card, bg=BG_PANEL)
        row.pack(fill="x", pady=6)

        switch = CmdToggle(
            row,
            variable=self.hw_vars[item["id"]],
            font=(self.mono, 10, "bold"),
            command=self._on_selection_changed,
            parent_bg=BG_PANEL,
        )
        switch.pack(side="left", anchor="n")

        block = tk.Frame(row, bg=BG_PANEL)
        block.pack(side="left", fill="x", expand=True, padx=(12, 0))

        tk.Label(
            block,
            text=item["name"],
            bg=BG_PANEL,
            fg=GREEN,
            font=(self.mono, 10),
            anchor="w",
            justify="left",
            wraplength=330,
        ).pack(anchor="w")
        tk.Label(
            block,
            text=item["pins_compact"],
            bg=BG_PANEL,
            fg=GREEN_DIM,
            font=(self.mono, 8),
            anchor="w",
            justify="left",
            wraplength=340,
        ).pack(anchor="w", pady=(2, 0))

        if item["required"]:
            tk.Label(
                block, text="<NOYAU REQUIS>", bg=BG_PANEL, fg=AMBER, font=(self.mono, 8, "bold"), anchor="w"
            ).pack(anchor="w")
        elif item["requires"]:
            dep_names = ", ".join(d.upper() for d in item["requires"])
            tk.Label(
                block, text=f"<REQUIERT {dep_names}>", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 8, "bold"), anchor="w"
            ).pack(anchor="w")

        if item["required"]:
            switch.set_enabled(False)
        self.switches[item["id"]] = switch

    def _toggle_select_all(self):
        optional_items = [item for item in HARDWARE_CATALOG if not item["required"]]
        all_selected = all(self.hw_vars[item["id"]].get() for item in optional_items)
        target_state = not all_selected
        for item in optional_items:
            self.hw_vars[item["id"]].set(target_state)
        self._enforce_dependencies()
        self._update_select_all_button()
        self._refresh_preview_and_status()

    def _update_select_all_button(self):
        optional_items = [item for item in HARDWARE_CATALOG if not item["required"]]
        all_selected = all(self.hw_vars[item["id"]].get() for item in optional_items)
        self.btn_toggle_all.set_text("Tout désélectionner" if all_selected else "Tout sélectionner")

    def _on_selection_changed(self):
        self._enforce_dependencies()
        self._update_select_all_button()
        self._refresh_preview_and_status()

    def _enforce_dependencies(self):
        for item in HARDWARE_CATALOG:
            if item["required"]:
                continue
            deps_ok = all(self.hw_vars[dep_id].get() for dep_id in item["requires"])
            if not deps_ok:
                self.hw_vars[item["id"]].set(False)
            switch = self.switches.get(item["id"])
            if switch:
                switch.set_enabled(deps_ok)

    # --------------------------------------------------------------------------
    # PAGE 3 : BROCHES DE LA CARTE
    # --------------------------------------------------------------------------
    def _pin_owners(self) -> dict[str, list[dict]]:
        """Associe chaque broche aux fonctions des modules actifs qui l'utilisent."""
        owners: dict[str, list[dict]] = {}
        for item in HARDWARE_CATALOG:
            if not self.hw_vars[item["id"]].get():
                continue
            for key, short, long_txt in item["pin_roles"]:
                owners.setdefault(key, []).append({"item": item, "short": short, "long": long_txt})
        return owners

    def _conflicts(self, owners: dict[str, list[dict]]) -> list[tuple[str, list[dict]]]:
        return [
            (key, entries)
            for key, entries in owners.items()
            if len(entries) > 1 and key not in SHARED_BUS_PINS
        ]

    def _pin_groups(self, view: str) -> list[tuple[str, list[str]]]:
        if view == VIEW_FULL:
            return [("DIGITALES D0 - D53", DIGITAL_KEYS), ("ANALOGIQUES A0 - A15", ANALOG_KEYS)]
        if view == VIEW_DIGITAL:
            return [("DIGITALES D0 - D53", DIGITAL_KEYS)]
        if view == VIEW_ANALOG:
            return [("ANALOGIQUES A0 - A15", ANALOG_KEYS)]
        groups = [(name, keys) for name, keys in BUS_GROUPS]
        used = {k for _n, keys in BUS_GROUPS for k in keys}
        others = [k for k in DIGITAL_KEYS if k not in used]
        groups.append(("AUTRES E/S DIGITALES (commande directe)", others))
        return groups

    def _pin_state(self, key: str, entries: list[dict]) -> str:
        if not entries and key in RESERVED_PINS:
            return "reserved"
        if len(entries) > 1:
            return "shared" if key in SHARED_BUS_PINS else "conflict"
        if entries:
            return "used"
        return "free"

    def _describe_pin(self, key: str) -> str:
        entries = self._pin_owners().get(key, [])
        if not entries and key in RESERVED_PINS:
            return f"{key} : réservée ({RESERVED_PINS[key]})"
        if not entries:
            return f"{key} : libre, disponible pour un nouveau module"
        state = self._pin_state(key, entries)
        parts = [f"{e['long']}  [{e['item']['name']}]" for e in entries]
        suffix = {
            "shared": "   [bus partagé]",
            "conflict": "   [CONFLIT : à corriger]",
        }.get(state, "")
        return f"{key} : " + "  |  ".join(parts) + suffix

    def _pin_hover(self, key: str):
        self.pin_info.config(text=self._describe_pin(key))

    def _build_pins_page(self, parent: tk.Frame):
        bar = tk.Frame(parent, bg=BLACK)
        bar.pack(fill="x", pady=(0, 10))
        tk.Label(bar, text="vue >", bg=BLACK, fg=GREEN_DIM, font=(self.mono, 10)).pack(side="left")

        self.pin_view_var = tk.StringVar(value=VIEW_FULL)
        combo = ttk.Combobox(
            bar,
            textvariable=self.pin_view_var,
            values=PIN_VIEWS,
            state="readonly",
            width=58,
            style="Cmd.TCombobox",
        )
        combo.pack(side="left", padx=10)
        combo.bind("<<ComboboxSelected>>", lambda _e: self._refresh_pin_map())

        legend = tk.Frame(parent, bg=BLACK)
        legend.pack(fill="x", pady=(0, 10))
        for text, color in (
            ("[ libre ]", GREEN_DIM),
            ("[ utilisée ]", GREEN_BRIGHT),
            ("[ partagée ]", AMBER),
            ("[ conflit ]", RED),
            ("[ réservée ]", GREY),
        ):
            tk.Label(legend, text=text, bg=BLACK, fg=color, font=(self.mono, 9, "bold")).pack(side="left", padx=(0, 14))

        box = self._make_box(parent, padx=0, pady=0)
        box.pack(fill="both", expand=True)

        self.pin_canvas = tk.Canvas(box, bg=BLACK, highlightthickness=0, bd=0)
        sb = CmdScrollbar(box, command=self.pin_canvas.yview, parent_bg=BG_PANEL)
        self.pin_canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y", padx=2, pady=2)
        self.pin_canvas.pack(side="left", fill="both", expand=True, padx=10, pady=10)
        attach_mousewheel(self.pin_canvas, self.pin_canvas)

        self.pin_info = tk.Label(
            parent,
            text=self.PIN_IDLE_TEXT,
            bg=BLACK,
            fg=GREEN_BRIGHT,
            font=(self.mono, 10),
            anchor="w",
            justify="left",
            wraplength=960,
        )
        self.pin_info.pack(fill="x", pady=(10, 0))

    def _refresh_pin_map(self):
        if not hasattr(self, "pin_canvas"):
            return
        view = self.pin_view_var.get()
        owners = self._pin_owners()
        canvas = self.pin_canvas
        canvas.delete("all")

        x0, y = 16, 16
        max_right = x0
        for title, keys in self._pin_groups(view):
            canvas.create_text(x0, y, text=f"> {title}", anchor="nw", fill=GREEN_BRIGHT, font=(self.mono, 10, "bold"))
            y += 28
            for row_keys in _chunks(keys, PIN_COLS):
                for col, key in enumerate(row_keys):
                    x = x0 + col * (PIN_CELL_W + PIN_GAP)
                    self._draw_pin_cell(canvas, key, x, y, owners.get(key, []))
                max_right = max(max_right, x0 + len(row_keys) * (PIN_CELL_W + PIN_GAP))
                y += PIN_CELL_H + PIN_GAP
            y += 18

        canvas.configure(scrollregion=(0, 0, max_right + 16, y + 8))
        canvas.yview_moveto(0)

    def _draw_pin_cell(self, canvas: tk.Canvas, key: str, x: int, y: int, entries: list[dict]):
        state = self._pin_state(key, entries)
        fill, border, key_fg, role_fg, tag_fg = {
            "free": (BLACK, GREEN_BORDER, GREEN_MUTED, GREEN_DIM, GREEN_MUTED),
            "used": (GREEN_BORDER, GREEN, GREEN_BRIGHT, GREEN_BRIGHT, GREEN),
            "shared": (SHARED_FILL, AMBER, AMBER, AMBER, AMBER),
            "conflict": (RED, RED, BLACK, BLACK, BLACK),
            "reserved": (BLACK, GREY, GREY, GREY, GREY),
        }[state]

        if state == "free":
            role_text, tag_text = "libre", ""
        elif state == "reserved":
            role_text, tag_text = "RÉSERVÉE", _fit(RESERVED_PINS[key], 16)
        elif state == "used":
            role_text = _fit(entries[0]["short"], 14)
            tag_text = _fit(entries[0]["item"]["tag"], 14)
        elif state == "shared":
            shorts = list(dict.fromkeys(e["short"] for e in entries))
            role_text = _fit("/".join(shorts), 14)
            tag_text = "PARTAGÉ"
        else:  # conflict
            shorts = list(dict.fromkeys(e["short"] for e in entries))
            role_text = "CONFLIT"
            tag_text = _fit("/".join(shorts), 14)

        tag = f"pin_{key}"
        canvas.create_rectangle(x, y, x + PIN_CELL_W, y + PIN_CELL_H, fill=fill, outline=border, width=1, tags=(tag,))
        canvas.create_text(
            x + PIN_CELL_W / 2, y + 16, text=key, fill=key_fg, font=(self.mono, 9, "bold"), tags=(tag,)
        )
        canvas.create_text(
            x + PIN_CELL_W / 2, y + 36, text=role_text, fill=role_fg, font=(self.mono, 8, "bold"), tags=(tag,)
        )
        canvas.create_text(
            x + PIN_CELL_W / 2, y + 52, text=tag_text, fill=tag_fg, font=(self.mono, 7), tags=(tag,)
        )
        canvas.tag_bind(tag, "<Enter>", lambda _e, k=key: self._pin_hover(k))
        canvas.tag_bind(tag, "<Leave>", lambda _e: self.pin_info.config(text=self.PIN_IDLE_TEXT))

    # --------------------------------------------------------------------------
    # PAGE 4 : RESSOURCES (RAM, FLASH, BROCHES, TÂCHES)
    # --------------------------------------------------------------------------
    def _build_resources_page(self, parent: tk.Frame):
        grid = tk.Frame(parent, bg=BLACK)
        grid.pack(fill="both", expand=True)
        grid.grid_columnconfigure(0, weight=1, uniform="res")
        grid.grid_columnconfigure(1, weight=1, uniform="res")
        grid.grid_rowconfigure(0, weight=1)
        grid.grid_rowconfigure(1, weight=1)

        mem = self._make_box(grid, padx=22, pady=18)
        mem.grid(row=0, column=0, sticky="nsew", padx=(0, 10), pady=(0, 14))
        tk.Label(mem, text="> MÉMOIRE SRAM", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w").pack(anchor="w")
        self.res_ram_bar = CmdProgress(mem, maximum=RAM_TOTAL, font=(self.mono, 10), parent_bg=BG_PANEL)
        self.res_ram_bar.pack(anchor="w", pady=(10, 6))
        self.res_ram_text = self._info_label(mem, wrap=520)
        self.res_ram_text.pack(anchor="w")
        self.res_ram_src = self._info_label(mem, fg=GREEN_DIM, wrap=520)
        self.res_ram_src.pack(anchor="w", pady=(6, 0))

        tk.Frame(mem, bg=GREEN_BORDER, height=1).pack(fill="x", pady=16)

        tk.Label(mem, text="> MÉMOIRE FLASH (ROM)", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w").pack(anchor="w")
        self.res_rom_bar = CmdProgress(mem, maximum=FLASH_TOTAL, font=(self.mono, 10), parent_bg=BG_PANEL)
        self.res_rom_bar.pack(anchor="w", pady=(10, 6))
        self.res_rom_text = self._info_label(mem, wrap=520)
        self.res_rom_text.pack(anchor="w")
        self.res_rom_src = self._info_label(mem, fg=GREEN_DIM, wrap=520)
        self.res_rom_src.pack(anchor="w", pady=(6, 0))

        pins = self._make_box(grid, padx=22, pady=18)
        pins.grid(row=0, column=1, sticky="nsew", padx=(10, 0), pady=(0, 14))
        tk.Label(pins, text="> BROCHES RESTANTES", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w").pack(anchor="w")
        self.res_pins_text = self._info_label(pins, wrap=500)
        self.res_pins_text.pack(anchor="w", pady=(10, 0))
        tk.Label(
            pins,
            text="Alimentations (5V, GND, VIN, AREF, RESET) exclues du décompte.",
            bg=BG_PANEL,
            fg=GREEN_MUTED,
            font=(self.mono, 8),
            anchor="w",
            justify="left",
            wraplength=500,
        ).pack(anchor="w", pady=(14, 0))

        bottom = self._make_box(grid, padx=22, pady=18)
        bottom.grid(row=1, column=0, columnspan=2, sticky="nsew")
        tk.Label(bottom, text="> CONFLITS DE BROCHES", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w").pack(anchor="w")
        self.res_conflicts_text = self._info_label(bottom, wrap=1000)
        self.res_conflicts_text.pack(anchor="w", pady=(8, 16))
        tk.Label(bottom, text="> TÂCHES FREERTOS ACTIVES", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w").pack(anchor="w")
        self.res_tasks_text = self._info_label(bottom, wrap=1000)
        self.res_tasks_text.pack(anchor="w", pady=(8, 0))

    def _info_label(self, parent, text: str = "", fg: str = GREEN, wrap: int = 0, bold: bool = False):
        font = (self.mono, 10, "bold") if bold else (self.mono, 10)
        return tk.Label(parent, text=text, bg=BG_PANEL, fg=fg, font=font, anchor="w", justify="left", wraplength=wrap)

    def _refresh_resources(self):
        if not hasattr(self, "res_ram_bar"):
            return
        active = [item for item in HARDWARE_CATALOG if self.hw_vars[item["id"]].get()]

        if "ram" in self.measured:
            used, total = self.measured["ram"]
            self.res_ram_bar.set_value(used, total)
            self.res_ram_text.config(text=f"{used} o utilisés sur {total} o  ({used * 100 / total:.1f} %)")
            self.res_ram_src.config(text="Source : mesure PlatformIO de la dernière compilation.")
        else:
            est = RAM_BASE_BYTES + sum(item["ram_words"] * 2 for item in active)
            self.res_ram_bar.set_value(est, RAM_TOTAL)
            self.res_ram_text.config(text=f"~{est} o estimés sur {RAM_TOTAL} o  ({est * 100 / RAM_TOTAL:.1f} %)")
            self.res_ram_src.config(text="Source : estimation. Compile pour obtenir la valeur exacte.")

        if "flash" in self.measured:
            used, total = self.measured["flash"]
            self.res_rom_bar.set_value(used, total)
            self.res_rom_text.config(text=f"{used} o utilisés sur {total} o  ({used * 100 / total:.1f} %)")
            self.res_rom_src.config(text="Source : mesure PlatformIO de la dernière compilation.")
        else:
            est = ROM_BASE_BYTES + sum(ROM_ESTIMATE.get(item["id"], 0) for item in active)
            self.res_rom_bar.set_value(est, FLASH_TOTAL)
            self.res_rom_text.config(text=f"~{est} o estimés sur {FLASH_TOTAL} o  ({est * 100 / FLASH_TOTAL:.1f} %)")
            self.res_rom_src.config(text="Source : estimation. Compile pour obtenir la valeur exacte.")

        owners = self._pin_owners()
        free_d = [k for k in DIGITAL_KEYS if k not in owners and k not in RESERVED_PINS]
        free_a = [k for k in ANALOG_KEYS if k not in owners]
        usable_d = len(DIGITAL_KEYS) - len(RESERVED_PINS)
        reserved_txt = ", ".join(f"{k} ({v})" for k, v in RESERVED_PINS.items())
        self.res_pins_text.config(
            text=(
                f"Digitales libres   : {len(free_d)} / {usable_d}\n"
                f"  {', '.join(free_d) or 'aucune'}\n\n"
                f"Analogiques libres : {len(free_a)} / {len(ANALOG_KEYS)}\n"
                f"  {', '.join(free_a) or 'aucune'}\n\n"
                f"Réservées          : {reserved_txt}"
            )
        )

        conflicts = self._conflicts(owners)
        if conflicts:
            lines = []
            for key, entries in conflicts:
                funcs = " / ".join(f"{e['long']}" for e in entries)
                lines.append(f"{key} : {funcs}")
            self.res_conflicts_text.config(text="\n".join(lines), fg=RED)
        else:
            self.res_conflicts_text.config(text="Aucun conflit détecté sur les broches.", fg=GREEN)

        tasks, _ = self._compute_tasks()
        self.res_tasks_text.config(text=f"{len(tasks)} tâche(s) : {', '.join(tasks)}")

    # --------------------------------------------------------------------------
    # PAGE 5 : GÉNÉRATION & COMPILATION
    # --------------------------------------------------------------------------
    def _build_build_page(self, parent: tk.Frame):
        parent.grid_columnconfigure(0, weight=1, uniform="build")
        parent.grid_columnconfigure(1, weight=1, uniform="build")
        parent.grid_rowconfigure(0, weight=1)

        left = self._make_box(parent, padx=18, pady=14)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        tk.Label(
            left, text="> include/hardware_config.h", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
        ).pack(anchor="w", pady=(0, 10))

        preview_frame, self.preview_text = make_text(left, BLACK, GREEN, (self.mono, 9), height=24)
        preview_frame.pack(fill="both", expand=True)
        self.preview_text.tag_configure("comment", foreground=GREEN_MUTED)
        self.preview_text.tag_configure("directive", foreground=GREEN_BRIGHT, font=(self.mono, 9, "bold"))
        self.preview_text.tag_configure("macro", foreground=GREEN)
        self.preview_text.tag_configure("val_on", foreground=GREEN_BRIGHT, font=(self.mono, 9, "bold"))
        self.preview_text.tag_configure("val_off", foreground=RED, font=(self.mono, 9, "bold"))

        right = self._make_box(parent, padx=18, pady=14)
        right.grid(row=0, column=1, sticky="nsew", padx=(10, 0))
        tk.Label(
            right, text="> COMPILATION PLATFORMIO", bg=BG_PANEL, fg=GREEN_BRIGHT, font=(self.mono, 10, "bold"), anchor="w"
        ).pack(anchor="w", pady=(0, 12))

        btn_bar = tk.Frame(right, bg=BG_PANEL)
        btn_bar.pack(fill="x", pady=(0, 12))

        self.btn_generate = CmdButton(
            btn_bar,
            text="Générer .h  F6",
            command=self._generate_header_only,
            kind="normal",
            font=(self.mono, 10, "bold"),
            parent_bg=BG_PANEL,
        )
        self.btn_generate.pack(side="left", padx=(0, 12))

        self.btn_compile = CmdButton(
            btn_bar,
            text="Compiler  F7",
            command=lambda: self._run_platformio(upload=False),
            kind="primary",
            font=(self.mono, 10, "bold"),
            parent_bg=BG_PANEL,
        )
        self.btn_compile.pack(side="left", padx=(0, 12))

        self.btn_upload = CmdButton(
            btn_bar,
            text="Téléverser  F8",
            command=lambda: self._run_platformio(upload=True),
            kind="warning",
            font=(self.mono, 10, "bold"),
            parent_bg=BG_PANEL,
        )
        self.btn_upload.pack(side="left")

        console_frame, self.console = make_text(right, BLACK, GREEN, (self.mono, 9), height=24)
        console_frame.pack(fill="both", expand=True)
        self.console.tag_configure("ok", foreground=GREEN_BRIGHT, font=(self.mono, 9, "bold"))
        self.console.tag_configure("err", foreground=RED, font=(self.mono, 9, "bold"))
        self.console.tag_configure("cmd", foreground=AMBER, font=(self.mono, 9, "bold"))

    # --------------------------------------------------------------------------
    # FENÊTRE À PROPOS
    # --------------------------------------------------------------------------
    def _show_version_dialog(self):
        dlg = tk.Toplevel(self)
        dlg.title(f"A propos - {APP_NAME}")
        dlg.configure(bg=BLACK)
        dlg.resizable(False, False)
        dlg.transient(self)
        dlg.grab_set()

        width, height = 620, 340
        x = self.winfo_rootx() + (self.winfo_width() - width) // 2
        y = self.winfo_rooty() + (self.winfo_height() - height) // 2
        dlg.geometry(f"{width}x{height}+{max(0, x)}+{max(0, y)}")

        tk.Label(
            dlg,
            text=f"{APP_NAME} [Version {APP_VERSION}]",
            bg=BLACK,
            fg=GREEN_BRIGHT,
            font=(self.mono, 12, "bold"),
            anchor="w",
        ).pack(fill="x", padx=24, pady=(22, 4))
        tk.Label(
            dlg,
            text=f"Edition {APP_BUILD_DATE}  |  {TARGET_FIRMWARE}",
            bg=BLACK,
            fg=GREEN_DIM,
            font=(self.mono, 9),
            anchor="w",
        ).pack(fill="x", padx=24)
        tk.Frame(dlg, bg=GREEN_BORDER, height=1).pack(fill="x", padx=24, pady=12)

        active_count = sum(1 for v in self.hw_vars.values() if v.get())
        total_count = len(HARDWARE_CATALOG)
        infos = [
            ("Version du script", f"v{APP_VERSION} ({APP_BUILD_DATE})"),
            ("Python", f"{platform.python_version()}"),
            ("Tk", f"{tk.TkVersion}"),
            ("Cible", "Arduino Mega 2560 (ATmega2560)"),
            ("Modules actifs", f"{active_count} / {total_count}"),
            ("Fichier genere", "include/hardware_config.h"),
        ]
        body = tk.Frame(dlg, bg=BLACK)
        body.pack(fill="x", padx=24)
        for label_txt, val_txt in infos:
            tk.Label(
                body,
                text=f"{label_txt:<20}: {val_txt}",
                bg=BLACK,
                fg=GREEN,
                font=(self.mono, 10),
                anchor="w",
            ).pack(fill="x")

        footer = tk.Frame(dlg, bg=BLACK)
        footer.pack(side="bottom", fill="x", padx=24, pady=18)
        tk.Label(footer, text="Appuyer sur ECHAP pour fermer", bg=BLACK, fg=GREEN_DIM, font=(self.mono, 9)).pack(side="left")
        CmdButton(
            footer,
            text="OK",
            command=dlg.destroy,
            kind="primary",
            font=(self.mono, 10, "bold"),
            parent_bg=BLACK,
        ).pack(side="right")
        dlg.bind("<Escape>", lambda _e: dlg.destroy())

    # --------------------------------------------------------------------------
    # GÉNÉRATION DU HEADER & SYNTHÈSE
    # --------------------------------------------------------------------------
    def _build_header_content(self) -> str:
        now = datetime.now().strftime("%d/%m/%Y %H:%M:%S")
        has_temp = 1 if (self.hw_vars["temp_mcp9808"].get() or self.hw_vars["temp_dht11"].get()) else 0
        lines = [
            "#pragma once",
            f"// Fichier généré automatiquement par {APP_NAME} v{APP_VERSION}",
            f"// Date : {now}",
            "",
        ]
        for item in HARDWARE_CATALOG:
            val = 1 if self.hw_vars[item["id"]].get() else 0
            lines.append(f"// {item['name']}")
            lines.append(f"#define {item['macro']} {val}")
            lines.append("")
        lines.append("// Macro dérivée : active TaskTemperature si au moins un capteur thermique est présent")
        lines.append(f"#define HW_HAS_TEMPERATURE {has_temp}")
        lines.append("")
        return "\n".join(lines)

    def _render_highlighted_preview(self, header_code: str):
        self.preview_text.config(state="normal")
        self.preview_text.delete("1.0", "end")
        for line in header_code.splitlines():
            stripped = line.strip()
            if stripped.startswith("//"):
                self.preview_text.insert("end", line + "\n", "comment")
            elif stripped.startswith("#pragma"):
                self.preview_text.insert("end", line + "\n", "directive")
            elif stripped.startswith("#define "):
                parts = stripped.split()
                if len(parts) == 3:
                    self.preview_text.insert("end", parts[0] + " ", "directive")
                    self.preview_text.insert("end", parts[1] + " ", "macro")
                    val_tag = "val_on" if parts[2] == "1" else "val_off"
                    self.preview_text.insert("end", parts[2] + "\n", val_tag)
                else:
                    self.preview_text.insert("end", line + "\n", "directive")
            else:
                self.preview_text.insert("end", line + "\n")
        self.preview_text.config(state="disabled")

    def _refresh_stats(self) -> tuple[int, int]:
        active = [item for item in HARDWARE_CATALOG if self.hw_vars[item["id"]].get()]
        pins = set()
        for item in active:
            pins.update(item["pin_keys"])
        self.stat_labels["active"].config(text=str(len(active)))
        self.stat_labels["pins"].config(text=str(len(pins)))
        return len(active), len(pins)

    def _compute_tasks(self) -> tuple[list[str], int]:
        tasks = ["TaskSafety", "TaskTraction", "TaskCommunication"]
        saved_words = 0
        if self.hw_vars["tourelle_std"].get():
            tasks.append("TaskTourelle")
        else:
            saved_words += 300
        if self.hw_vars["temp_mcp9808"].get() or self.hw_vars["temp_dht11"].get():
            tasks.append("TaskTemperature")
        else:
            saved_words += 200
        if self.hw_vars["rfid_rc522"].get():
            tasks.append("TaskAdmin")
        else:
            saved_words += 300
        return tasks, saved_words

    def _refresh_preview_and_status(self):
        tasks, saved_words = self._compute_tasks()
        saved_bytes = saved_words * 2  # configSTACK_DEPTH_TYPE = uint16_t sur AVR
        active_count, pin_count = self._refresh_stats()

        self.status_label.config(
            text=(
                f"TASKS ({len(tasks)}) : {', '.join(tasks)}\n"
                f"MODULES : {active_count} / {len(HARDWARE_CATALOG)}"
            )
        )
        self.ram_badge_label.config(text=f"SRAM économisée : {saved_bytes} o sur 8 192 o")
        self.ram_progress.set_value(saved_bytes, 1600)

        self.status_bar.config(
            text=(
                " F1-F5 pages | F6 générer .h | F7 compiler | F8 téléverser"
                f"   |   modules {active_count}/{len(HARDWARE_CATALOG)}"
                f"   |   broches {pin_count}"
            )
        )
        self._render_highlighted_preview(self._build_header_content())
        self._refresh_pin_map()
        self._refresh_resources()

    # --------------------------------------------------------------------------
    # CONSOLE & PLATFORMIO
    # --------------------------------------------------------------------------
    def _log_console(self, text: str, tag: str | None = None):
        self.console.config(state="normal")
        if tag:
            self.console.insert("end", text, tag)
        else:
            self.console.insert("end", text)
        self.console.see("end")
        self.console.config(state="disabled")

    def _poll_queue(self):
        """Relit les messages des threads de compilation dans le thread Tk."""
        try:
            while True:
                msg = self._ui_queue.get_nowait()
                kind = msg[0]
                if kind == "log":
                    self._log_console(msg[1], msg[2])
                elif kind == "buttons":
                    self._set_buttons_state(msg[1])
                elif kind == "measure":
                    self.measured[msg[1]] = (msg[2], msg[3])
                    self._refresh_resources()
        except queue.Empty:
            pass
        self.after(50, self._poll_queue)

    def _generate_header_only(self) -> bool:
        self.header_output_path.parent.mkdir(parents=True, exist_ok=True)
        self.header_output_path.write_text(self._build_header_content(), encoding="utf-8")
        self._save_selection()
        self._log_console(f"{PROMPT_PATH} generate .h\n", "cmd")
        self._log_console(f"[OK] Fichier généré : {self.header_output_path}\n", "ok")
        return True

    def _find_pio_executable(self) -> str | None:
        pio = shutil.which("pio") or shutil.which("platformio")
        if pio:
            return pio
        home = Path.home()
        for candidate in (
            home / ".platformio" / "penv" / "Scripts" / "pio.exe",
            home / ".platformio" / "penv" / "bin" / "pio",
        ):
            if candidate.exists():
                return str(candidate)
        return None

    def _set_buttons_state(self, state: str):
        enabled = state == "normal"
        self._busy = not enabled
        for btn in (self.btn_generate, self.btn_compile, self.btn_upload):
            btn.set_enabled(enabled)

    def _run_platformio(self, upload: bool):
        if self._busy:
            return
        if not self._generate_header_only():
            return
        pio_exe = self._find_pio_executable()
        if not pio_exe:
            self._log_console("[ERREUR] Impossible de trouver l'exécutable PlatformIO ('pio').\n", "err")
            return

        cmd = [pio_exe, "run", "-e", "megaatmega2560"]
        if upload:
            cmd.extend(["-t", "upload"])
        self._set_buttons_state("disabled")
        self._log_console(f"\n{PROMPT_PATH} {' '.join(cmd)}\n", "cmd")

        def worker():
            q = self._ui_queue
            try:
                proc = subprocess.Popen(
                    cmd,
                    cwd=str(self.project_dir),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                assert proc.stdout is not None
                for line in proc.stdout:
                    q.put(("log", line, None))
                    ram = RAM_RE.search(line)
                    if ram:
                        q.put(("measure", "ram", int(ram.group(1)), int(ram.group(2))))
                    flash = FLASH_RE.search(line)
                    if flash:
                        q.put(("measure", "flash", int(flash.group(1)), int(flash.group(2))))
                code = proc.wait()
                if code == 0:
                    q.put(("log", "\n[SUCCÈS] Opération terminée sans erreur.\n", "ok"))
                else:
                    q.put(("log", f"\n[ERREUR] Échec PlatformIO (code {code}).\n", "err"))
            except Exception as exc:
                q.put(("log", f"\n[EXCEPTION] {exc}\n", "err"))
            finally:
                q.put(("buttons", "normal"))

        threading.Thread(target=worker, daemon=True).start()


if __name__ == "__main__":
    TankOSConfigurator().mainloop()