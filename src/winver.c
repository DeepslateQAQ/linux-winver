/*
 * winver — a winver-style "About" dialog for Linux distributions (GTK4).
 *
 * Mirrors the layout of the Windows winver dialog:
 *   distro logo banner / divider / distro name / version / license terms /
 *   "licensed to" user / OK button (bottom right).
 *
 * The UI language follows the process locale (zh* -> Chinese, anything else
 * -> English); override with --lang=zh|en|auto.
 *
 * Build:  make
 * Run:    ./winver [--lang=zh|en]
 */

#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include <locale.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gchar *id;         /* os-release ID=           */
    gchar *name;       /* os-release NAME=         */
    gchar *pretty;     /* os-release PRETTY_NAME=  */
    gchar *version_id; /* os-release VERSION_ID=   */
    gchar *logo;       /* os-release LOGO=         */
    gchar *kernel;     /* kernel release (uname)   */
    gchar *username;   /* licensed-to user         */
    gchar *license_path; /* detected license file  */
    gchar *license_name; /* human-readable name    */
    gchar *license_note; /* text when no file      */
    gboolean zh;       /* UI language              */
    int year;          /* copyright year (0 = auto) */
    GtkWidget *content;  /* content box, rebuilt on demo switch */
    GtkWidget *demo_bar; /* demo picker row (demo mode only) */
} WinverInfo;

static gchar lang_override[16] = {0};
static gboolean demo_mode = FALSE;

/* ---------------- os-release ---------------- */

/* Strip a single pair of surrounding quotes and process the escapes that
 * os-release(5) allows inside quoted values. */
static gchar *unquote_value(gchar *v)
{
    gsize n = strlen(v);
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        gchar *out = g_malloc(n + 1);
        gchar *o = out;
        for (gsize i = 1; i + 1 < n; i++) {
            if (v[i] == '\\' && i + 2 < n && strchr("\"\\$`", v[i + 1]))
                *o++ = v[++i];
            else
                *o++ = v[i];
        }
        *o = '\0';
        g_free(v);
        return out;
    }
    return v;
}

static gchar *os_release_field(const gchar *content, const gchar *key)
{
    const gsize klen = strlen(key);
    const gchar *p = content;

    while (*p) {
        const gchar *nl = strchr(p, '\n');
        const gsize llen = nl ? (gsize)(nl - p) : strlen(p);

        if (llen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            gchar *val = g_strndup(p + klen + 1, llen - klen - 1);
            g_strstrip(val);
            return unquote_value(val);
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return NULL;
}

static void load_os_release(WinverInfo *info)
{
    static const gchar *const paths[] = {
        "/etc/os-release",
        "/usr/lib/os-release",
    };
    gchar *content = NULL;

    for (gsize i = 0; i < G_N_ELEMENTS(paths) && !content; i++)
        g_file_get_contents(paths[i], &content, NULL, NULL);
    if (!content)
        return;

    info->id         = os_release_field(content, "ID");
    info->name       = os_release_field(content, "NAME");
    info->pretty     = os_release_field(content, "PRETTY_NAME");
    info->version_id = os_release_field(content, "VERSION_ID");
    info->logo       = os_release_field(content, "LOGO");
    g_free(content);

    if (!info->name)
        info->name = g_strdup("Linux");
    if (!info->pretty)
        info->pretty = g_strdup(info->name);
    if (!info->version_id)
        info->version_id = g_strdup("unknown");
}

/* ---------------- license ---------------- */

/* Locate the distribution's license text and its display name. */
static void load_license(WinverInfo *info)
{
    static const struct { const gchar *path; const gchar *name; } common[] = {
        /* Debian / Ubuntu */
        { "/usr/share/common-licenses/GPL-3",     "GNU General Public License version 3" },
        { "/usr/share/common-licenses/GPL-2",     "GNU General Public License version 2" },
        { "/usr/share/common-licenses/GPL",       "GNU General Public License" },
        { "/usr/share/common-licenses/LGPL-3",    "GNU Lesser General Public License version 3" },
        { "/usr/share/common-licenses/LGPL-2.1",  "GNU Lesser General Public License version 2.1" },
        { "/usr/share/common-licenses/Apache-2.0","Apache License 2.0" },
        { "/usr/share/common-licenses/BSD",       "BSD License" },
        /* Arch Linux (licenses package) */
        { "/usr/share/licenses/spdx/GPL-3.0-or-later.txt", "GNU General Public License version 3 or later" },
        { "/usr/share/licenses/spdx/GPL-3.0-only.txt",     "GNU General Public License version 3" },
        { "/usr/share/licenses/spdx/GPL-2.0-or-later.txt", "GNU General Public License version 2 or later" },
        { "/usr/share/licenses/spdx/GPL-2.0-only.txt",     "GNU General Public License version 2" },
        /* Fedora common-licenses proposal / legacy */
        { "/usr/share/common-licenses/GPL-3.0/LICENSE",    "GNU General Public License version 3" },
        { "/usr/share/licenses/GPLv3",            "GNU General Public License version 3" },
        { "/usr/share/licenses/GPLv2",            "GNU General Public License version 2" },
        { "/usr/share/licenses/MIT",              "MIT License" },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(common); i++) {
        if (g_file_test(common[i].path, G_FILE_TEST_IS_REGULAR)) {
            info->license_path = g_strdup(common[i].path);
            info->license_name = g_strdup(common[i].name);
            return;
        }
    }

    /* NixOS has no system-wide license text on disk; nixpkgs (which the
     * whole system is built from) is MIT-licensed. */
    if (info->id && g_strcmp0(info->id, "nixos") == 0) {
        info->license_name = g_strdup("MIT License");
        info->license_note =
            g_strdup("NixOS is built from nixpkgs, which is released under the "
                     "MIT License.\nhttps://github.com/NixOS/nixpkgs/blob/master/COPYING");
    }
}

/* ---------------- demo distros ---------------- */

typedef struct {
    const gchar *id;          /* os-release ID (icon lookup) */
    const gchar *name;        /* NAME= */
    const gchar *pretty;      /* PRETTY_NAME= */
    const gchar *version_id;  /* VERSION_ID= */
    const gchar *kernel;      /* simulated OS Build */
    const gchar *license;     /* display name */
    int year;                 /* copyright year */
} DemoDistro;

static const DemoDistro demo_distros[] = {
    { "alpine",      "Alpine Linux",             "Alpine Linux 3.20",                       "3.20",   "6.6.49-0-lts",                  "MIT License",                                   2024 },
    { "arch",        "Arch Linux",               "Arch Linux",                              "Rolling Release", "6.9.7-arch1-1",     "GNU General Public License version 3 or later", 2025 },
    { "artix",       "Artix Linux",              "Artix Linux (Rolling Release)",           "Rolling Release", "6.9.7-artix1-1",  "GNU General Public License version 3 or later", 2025 },
    { "cachyos",     "CachyOS",                  "CachyOS (Rolling Release)",               "Rolling Release", "6.9.7-2-cachyos",  "GNU General Public License version 3 or later", 2025 },
    { "centos",      "CentOS Stream",            "CentOS Stream 9",                         "9",      "5.14.0-427.13.1.el9.x86_64",    "GNU General Public License version 2",          2024 },
    { "chromeos",    "ChromeOS",                 "ChromeOS 126",                            "126",    "6.1.73-chromeos",               "GNU General Public License version 2",          2024 },
    { "chromiumos",  "ChromiumOS",               "ChromiumOS 126",                          "126",    "6.1.73-chromiumos",             "GNU General Public License version 2",          2024 },
    { "debian",      "Debian GNU/Linux",         "Debian GNU/Linux 12 (bookworm)",          "12",     "6.1.0-22-amd64",                "GNU General Public License version 2",          2023 },
    { "deepin",      "Deepin",                   "Deepin 23",                               "23",     "6.6.34-amd64",                  "GNU General Public License version 2",          2024 },
    { "endeavouros", "EndeavourOS",              "EndeavourOS (Rolling Release)",           "Rolling Release", "6.9.7-arch1-1",   "GNU General Public License version 3 or later", 2025 },
    { "fedora",      "Fedora Linux",             "Fedora Linux 40 (Workstation Edition)",   "40",     "6.8.5-301.fc40.x86_64",         "GNU General Public License version 3",          2024 },
    { "gentoo",      "Gentoo Linux",             "Gentoo Linux",                            "Rolling Release", "6.9.3-gentoo",    "GNU General Public License version 2",          2025 },
    { "kubuntu",     "Kubuntu",                  "Kubuntu 24.04.2 LTS",                     "24.04.2","6.8.0-45-generic",              "GNU General Public License version 3",          2024 },
    { "lfs",         "Linux From Scratch",       "Linux From Scratch 12.2",                 "12.2",   "6.9.3-lfs-12.2",                "GNU General Public License version 2",          2024 },
    { "linuxmint",   "Linux Mint",               "Linux Mint 22 (Wilma)",                   "22",     "6.8.0-38-generic",              "GNU General Public License version 3",          2024 },
    { "manjaro",     "Manjaro Linux",            "Manjaro Linux 24.2 (Yonada)",             "24.2",   "6.9.6-1-MANJARO",               "GNU General Public License version 3",          2024 },
    { "nixos",       "NixOS",                    "NixOS 26.11 (Zokor)",                     "26.11",  "7.1.4-zen1",                    "MIT License",                                   2026 },
    { "nyarch",      "NyarchOS",                 "NyarchOS 24.06",                          "24.06",  "6.9.7-nyarch1-1",               "GNU General Public License version 3 or later", 2024 },
    { "opensuse",    "openSUSE Leap",            "openSUSE Leap 15.6",                      "15.6",   "6.4.0-150600.23.25-default",    "GNU General Public License version 2",          2024 },
    { "postmarketos","postmarketOS",             "postmarketOS 24.06",                      "24.06",  "6.6.32-r0",                     "GNU General Public License version 2",          2024 },
    { "raspbian",    "Raspbian GNU/Linux",       "Raspbian GNU/Linux 12 (bookworm)",        "12",     "6.1.0-rpi7-rpi-2712",           "GNU General Public License version 2",          2024 },
    { "rhel",        "Red Hat Enterprise Linux", "Red Hat Enterprise Linux 9.4 (Plow)",    "9.4",    "5.14.0-427.13.1.el9_4.x86_64",  "GNU General Public License version 2",          2024 },
    { "rocky",       "Rocky Linux",              "Rocky Linux 9.4 (Blue Onyx)",             "9.4",    "5.14.0-427.13.1.el9_4.x86_64",  "GNU General Public License version 2",          2024 },
    { "steamos",     "SteamOS",                  "SteamOS 3.6 (holo)",                      "3.6",    "6.1.52-valve16-1",              "GNU General Public License version 2",          2024 },
    { "ubuntu",      "Ubuntu",                   "Ubuntu 24.04.2 LTS (Noble Numbat)",       "24.04.2","6.8.0-45-generic",              "GNU General Public License version 3",          2024 },
    { "ubuntu-mate", "Ubuntu MATE",              "Ubuntu MATE 24.04 LTS",                   "24.04",  "6.8.0-45-generic",              "GNU General Public License version 3",          2024 },
    { "uos",         "UOS",                      "UOS Desktop 20.9",                        "20.9",   "5.10.0-amp2028-desktop",        "GNU General Public License version 2",          2023 },
    { "void",        "Void Linux",               "Void Linux (Rolling Release)",            "Rolling Release", "6.9.7_1",         "MIT License",                                   2025 },
    { "xubuntu",     "Xubuntu",                  "Xubuntu 24.04 LTS",                       "24.04",  "6.8.0-45-generic",              "GNU General Public License version 3",          2024 },
    { "zorin",       "Zorin OS",                 "Zorin OS 17.1",                           "17.1",   "6.5.0-17-generic",              "GNU General Public License version 3",          2024 },
};

static void reset_info(WinverInfo *info)
{
    g_clear_pointer(&info->id, g_free);
    g_clear_pointer(&info->name, g_free);
    g_clear_pointer(&info->pretty, g_free);
    g_clear_pointer(&info->version_id, g_free);
    g_clear_pointer(&info->logo, g_free);
    g_clear_pointer(&info->kernel, g_free);
    g_clear_pointer(&info->license_path, g_free);
    g_clear_pointer(&info->license_name, g_free);
    g_clear_pointer(&info->license_note, g_free);
    info->year = 0;
}

static void apply_demo(WinverInfo *info, const DemoDistro *d)
{
    reset_info(info);
    info->id = g_strdup(d->id);
    info->name = g_strdup(d->name);
    info->pretty = g_strdup(d->pretty);
    info->version_id = g_strdup(d->version_id);
    info->kernel = g_strdup(d->kernel);
    info->license_name = g_strdup(d->license);
    info->license_note = g_strdup_printf(
        info->zh ? "演示数据：%s 随发行版提供 %s 全文。\n（真实系统上此处显示已安装的许可文件内容。）"
                 : "Demo data: %s ships the full text of the %s.\n(On a real system, the installed license file is shown here.)",
        d->name, d->license);
    info->year = d->year;
}

static void reload_current(WinverInfo *info)
{
    reset_info(info);
    load_os_release(info);
    load_license(info);
    struct utsname u;
    info->kernel = (uname(&u) == 0) ? g_strdup(u.release) : g_strdup("unknown");
}

/* ---------------- locale ---------------- */

static const gchar *effective_lang(void)
{
    if (lang_override[0] && g_strcmp0(lang_override, "auto") != 0)
        return lang_override;

    const gchar *lang = getenv("LC_ALL");
    if (!lang || !*lang)
        lang = getenv("LC_MESSAGES");
    if (!lang || !*lang)
        lang = getenv("LANG");
    return (lang && *lang) ? lang : "en";
}

static gboolean is_zh(const gchar *lang)
{
    return strncmp(lang, "zh", 2) == 0;
}

/* ---------------- logo banner ---------------- */

static gboolean icon_available(const gchar *name)
{
    if (!name || !*name)
        return FALSE;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    return theme != NULL && gtk_icon_theme_has_icon(theme, name);
}

/* Well-known distro logo icon names, keyed by os-release ID. */
static const gchar *id_to_icon(const gchar *id)
{
    static const struct { const gchar *id; const gchar *icon; } map[] = {
        { "ubuntu",      "ubuntu-logo"      },
        { "ubuntu-mate", "ubuntu-logo"      },
        { "kubuntu",     "ubuntu-logo"      },
        { "xubuntu",     "ubuntu-logo"      },
        { "fedora",      "fedora-logo"      },
        { "arch",        "archlinux"        },
        { "archlinux",   "archlinux"        },
        { "manjaro",     "manjaro"          },
        { "debian",      "debian-logo"      },
        { "linuxmint",   "linuxmint-logo"   },
        { "opensuse",    "opensuse-logo"    },
        { "nixos",       "nixos-logo"       },
        { "pop",         "pop-os"           },
        { "zorin",       "zorin"            },
        { "endeavouros", "endeavouros"      },
        { "centos",      "centos"           },
        { "rocky",       "rocky"            },
        { "rhel",        "rhel"             },
        { "void",        "void"             },
        { "gentoo",      "gentoo"           },
        { "slackware",   "slackware"        },
        { "mageia",      "mageia"           },
        { "kali",        "kali"             },
        { "raspbian",    "raspberry-pi-logo" },
        { "deepin",      "deepin"           },
        { "uos",         "uos"              },
        { "cachyos",     "cachyos"          },
        { "artix",       "artix"            },
        { "steamos",     "steam"            },
        { "chromeos",    "chrome"           },
        { "chromiumos",  "chromium"         },
        { "alpine",      "alpine"           },
        { "lfs",         "linux"            },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(map); i++)
        if (id && g_strcmp0(id, map[i].id) == 0)
            return map[i].icon;
    return NULL;
}

/* Ellipse as a path: cairo has no direct ellipse primitive. */
static void cairo_ellipse_path(cairo_t *cr, double cx, double cy, double rx, double ry)
{
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1.0, 0, 2 * G_PI);
    cairo_restore(cr);
}

/* Fallback banner drawn when the icon theme has no distro logo:
 * a simple Tux silhouette. */
static void draw_fallback_logo(GtkDrawingArea *da, cairo_t *cr,
                               int width, int height, gpointer data)
{
    (void)da;
    (void)data;
    const double w = width, h = height;
    const double cx = w / 2, cy = h / 2;

    /* head + body */
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.13);
    cairo_ellipse_path(cr, cx, cy - 2, w * 0.30, h * 0.32);
    cairo_ellipse_path(cr, cx, cy + 16, w * 0.37, h * 0.30);
    cairo_fill(cr);

    /* belly */
    cairo_set_source_rgb(cr, 0.97, 0.97, 0.97);
    cairo_ellipse_path(cr, cx, cy + 18, w * 0.22, h * 0.20);
    cairo_fill(cr);

    /* eyes */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, cx - 11, cy - 16, 3.2, 0, 2 * G_PI);
    cairo_arc(cr, cx + 11, cy - 16, 3.2, 0, 2 * G_PI);
    cairo_fill(cr);

    /* beak */
    cairo_set_source_rgb(cr, 0.95, 0.55, 0.10);
    cairo_move_to(cr, cx - 9, cy - 10);
    cairo_line_to(cr, cx + 9, cy - 10);
    cairo_line_to(cr, cx, cy - 1);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* feet */
    cairo_ellipse_path(cr, cx - 13, cy + 34, 9, 5);
    cairo_ellipse_path(cr, cx + 13, cy + 34, 9, 5);
    cairo_fill(cr);
}

/* Look up a built-in icon resource for a distro id. */
static GtkWidget *resource_icon_for(const gchar *id)
{
    if (!id)
        return NULL;
    static const struct { const gchar *id; const gchar *file; } map[] = {
        { "alpine",       "alpine"       },
        { "arch",         "arch"         }, { "archlinux", "arch" },
        { "artix",        "artix"        },
        { "cachyos",      "cachyos"      },
        { "centos",       "centos"       },
        { "chromeos",     "chromeos"     },
        { "chromiumos",   "chromiumos"   },
        { "debian",       "debian"       },
        { "deepin",       "deepin"       },
        { "endeavouros",  "endeavour"    },
        { "fedora",       "fedora"       },
        { "gentoo",       "gentoo"       },
        { "kali",         "kali"         },
        { "kubuntu",      "kubuntu"      },
        { "lfs",          "lfs"          },
        { "linuxmint",    "mint"         },
        { "manjaro",      "manjaro"      },
        { "nixos",        "nixos"        },
        { "opensuse",     "opensuse"     },
        { "postmarketos", "postmarketos" },
        { "raspbian",     "raspbian"     },
        { "rhel",         "rhel"         },
        { "rocky",        "rocky"        },
        { "steamos",      "steamos"      },
        { "ubuntu",       "ubuntu"       },
        { "ubuntu-mate",  "ubuntu-mate"  },
        { "uos",          "uos"          },
        { "void",         "void"         },
        { "xubuntu",      "xubuntu"      },
        { "zorin",        "zorin"        },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(map); i++) {
        if (g_strcmp0(id, map[i].id) != 0)
            continue;
        gchar *path = g_strdup_printf("/icons/%s.png", map[i].file);
        GtkWidget *img = NULL;
        if (g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL) != NULL) {
            img = gtk_image_new_from_resource(path);
            gtk_image_set_pixel_size(GTK_IMAGE(img), 96);
        }
        g_free(path);
        return img;
    }
    return NULL;
}

static GtkWidget *make_logo(WinverInfo *info)
{
    const gchar *list[8];
    gsize n = 0;
    const gchar *mapped = id_to_icon(info->id);

    if (info->logo)
        list[n++] = info->logo;
    if (mapped)
        list[n++] = mapped;
    list[n++] = "distributor-logo";
    if (info->id) {
        gchar *a = g_strdup_printf("%s-logo", info->id);
        gchar *b = g_strdup(info->id);
        if (!mapped || g_strcmp0(mapped, a) != 0)
            list[n++] = a;
        else
            g_free(a);
        if (!mapped || g_strcmp0(mapped, b) != 0)
            list[n++] = b;
        else
            g_free(b);
    }

    for (gsize i = 0; i < n; i++) {
        if (icon_available(list[i])) {
            GtkWidget *img = gtk_image_new_from_icon_name(list[i]);
            gtk_image_set_pixel_size(GTK_IMAGE(img), 96);
            return img;
        }
    }

    /* last resort before drawing Tux: generic penguin icons */
    static const gchar *const generic[] = { "tux", "linux" };
    for (gsize i = 0; i < G_N_ELEMENTS(generic); i++) {
        if (icon_available(generic[i])) {
            GtkWidget *img = gtk_image_new_from_icon_name(generic[i]);
            gtk_image_set_pixel_size(GTK_IMAGE(img), 96);
            return img;
        }
    }

    /* built-in icon resources (distro logos), then the real Tux */
    if (info->id) {
        GtkWidget *img = resource_icon_for(info->id);
        if (img)
            return img;
    }
    if (g_resources_lookup_data("/icons/tux.png", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL) != NULL) {
        GtkWidget *img = gtk_image_new_from_resource("/icons/tux.png");
        gtk_image_set_pixel_size(GTK_IMAGE(img), 96);
        return img;
    }

    /* last resort: hand-drawn Tux */
    GtkWidget *da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da), 96);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), 96);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_fallback_logo, info, NULL);
    return da;
}

/* ---------------- dialog ---------------- */

static GtkWidget *info_label(const gchar *markup)
{
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 62);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    return label;
}

static void on_ok_clicked(GtkButton *button, gpointer win)
{
    (void)button;
    gtk_window_close(GTK_WINDOW(win));
}

static int version_year(const gchar *version_id)
{
    if (!version_id)
        return 0;
    for (const gchar *p = version_id; p[3]; p++) {
        if ((p[0] == '1' || p[0] == '2') &&
            g_ascii_isdigit(p[1]) && g_ascii_isdigit(p[2]) && g_ascii_isdigit(p[3])) {
            int y = (p[0] - '0') * 1000 + (p[1] - '0') * 100 +
                    (p[2] - '0') * 10 + (p[3] - '0');
            if (y >= 1990 && y <= 2100)
                return y;
        }
    }
    return 0;
}

static void on_dialog_close_clicked(GtkButton *button, gpointer win)
{
    (void)button;
    gtk_window_close(GTK_WINDOW(win));
}

/* Pop up a scrollable window with the full license text (or a short note
 * when no license file exists on the system). */
static void show_license_dialog(GtkWindow *parent, WinverInfo *info)
{
    gchar *content = NULL;
    if (info->license_path)
        g_file_get_contents(info->license_path, &content, NULL, NULL);
    if (!content)
        content = g_strdup(info->license_note ? info->license_note : info->license_name);

    GtkWidget *win = gtk_window_new();
    gchar *title = g_strdup_printf("%s — %s", info->license_name, info->name);
    gtk_window_set_title(GTK_WINDOW(win), title);
    g_free(title);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 420);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
    gtk_box_append(GTK_BOX(vbox), sw);

    GtkWidget *label = gtk_label_new(content);
    g_free(content);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), label);

    GtkWidget *close = gtk_button_new_with_label(info->zh ? "关闭" : "Close");
    gtk_widget_set_halign(close, GTK_ALIGN_END);
    gtk_widget_set_margin_top(close, 8);
    gtk_widget_set_margin_bottom(close, 8);
    gtk_widget_set_margin_end(close, 8);
    gtk_box_append(GTK_BOX(vbox), close);
    g_signal_connect(close, "clicked", G_CALLBACK(on_dialog_close_clicked), win);
    gtk_window_set_default_widget(GTK_WINDOW(win), close);

    gtk_window_present(GTK_WINDOW(win));
}

static gboolean on_activate_link(GtkLabel *label, const gchar *uri, gpointer data)
{
    (void)label;
    if (g_strcmp0(uri, "license:view") != 0)
        return FALSE;
    WinverInfo *info = data;
    if (info->license_name) {
        GtkWindow *root = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(label)));
        show_license_dialog(root, info);
    }
    return TRUE;
}

/* Rebuild the variable content (logo, heading, info lines, user).
 * Called once at startup and again whenever the demo picker changes. */
static GtkWidget *build_content(WinverInfo *info)
{
    const gboolean zh = info->zh;
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* logo banner + distro name, side by side like the real winver */
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_halign(head, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(head, 10);

    GtkWidget *logo = make_logo(info);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(head), logo);

    gchar *big = g_markup_printf_escaped("<span font='24' weight='bold'>%s</span>", info->pretty);
    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(name_label), big);
    g_free(big);
    /* single line, never wrapped: the window is wide enough */
    gtk_widget_set_valign(name_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(head), name_label);

    gtk_box_append(GTK_BOX(content), head);

    /* divider */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 14);
    gtk_widget_set_margin_bottom(sep, 14);
    gtk_box_append(GTK_BOX(content), sep);

    gchar *esc_name = g_markup_escape_text(info->name, -1);

    /* product line (the "Microsoft Windows" analog) */
    if (g_strcmp0(info->name, info->pretty) != 0) {
        gchar *m = g_strdup_printf("<span font='10'>%s</span>", esc_name);
        GtkWidget *l = info_label(m);
        g_free(m);
        gtk_box_append(GTK_BOX(content), l);
    }

    /* version line */
    gchar *m = g_markup_printf_escaped(
        zh ? "<span font='10'>版本 %s (OS 内部版本 %s)</span>"
           : "<span font='10'>Version %s (OS Build %s)</span>",
        info->version_id, info->kernel);
    GtkWidget *ver = info_label(m);
    g_free(m);
    gtk_box_append(GTK_BOX(content), ver);

    /* copyright — mirrors the license statement: no "All rights reserved"
     * for open-source software */
    int year = info->year;
    if (!year) {
        year = version_year(info->version_id);
        if (!year) {
            time_t t = time(NULL);
            year = localtime(&t)->tm_year + 1900;
        }
    }
    if (info->license_name) {
        m = g_markup_printf_escaped(
            zh ? "<span font='10'>© %d %s 项目贡献者。根据 %s 发布。</span>"
               : "<span font='10'>© %d %s Project contributors. Released under the %s.</span>",
            year, info->name, info->license_name);
    } else {
        m = g_markup_printf_escaped(
            zh ? "<span font='10'>© %d %s 项目贡献者。</span>"
               : "<span font='10'>© %d %s Project contributors.</span>",
            year, info->name);
    }
    GtkWidget *copy = info_label(m);
    g_free(m);
    gtk_widget_set_margin_top(copy, 12);
    gtk_box_append(GTK_BOX(content), copy);

    /* trademark */
    m = g_markup_printf_escaped(
        zh ? "<span font='10'>%s 操作系统及其用户界面受美国和其他国家/地区的商标和其他知识产权法保护。</span>"
           : "<span font='10'>The %s operating system and its user interface are protected by "
             "trademark and other pending or existing intellectual property rights in the "
             "United States and other countries/regions.</span>",
        info->name);
    GtkWidget *trademark = info_label(m);
    g_free(m);
    gtk_widget_set_margin_top(trademark, 4);
    gtk_box_append(GTK_BOX(content), trademark);

    /* license terms */
    if (info->license_name) {
        m = g_markup_printf_escaped(
            zh ? "<span font='10'>本产品根据 <a href='license:view'>%s</a> 授权给:</span>"
               : "<span font='10'>This product is licensed under the <a href='license:view'>%s</a> to:</span>",
            info->license_name);
        GtkWidget *license = info_label(m);
        g_free(m);
        g_signal_connect(license, "activate-link", G_CALLBACK(on_activate_link), info);
        gtk_widget_set_margin_top(license, 12);
        gtk_box_append(GTK_BOX(content), license);
    } else {
        m = g_strdup_printf(
            zh ? "<span font='10'>本产品根据 <u><span foreground='#0000ff'>开源软件许可条款</span></u> 授权给:</span>"
               : "<span font='10'>This product is licensed under the <u><span foreground='#0000ff'>open-source "
                 "software license terms</span></u> to:</span>");
        GtkWidget *license = info_label(m);
        g_free(m);
        gtk_widget_set_margin_top(license, 12);
        gtk_box_append(GTK_BOX(content), license);
    }

    /* licensed to */
    m = g_markup_printf_escaped("<span font='10' weight='bold'>%s</span>", info->username);
    GtkWidget *user = info_label(m);
    g_free(m);
    gtk_widget_set_margin_top(user, 6);
    gtk_box_append(GTK_BOX(content), user);

    g_free(esc_name);
    return content;
}

static void on_demo_selected(GObject *obj, GParamSpec *ps, gpointer data);

/* Demo picker row: current system + all simulated distros. */
static GtkWidget *make_demo_bar(WinverInfo *info)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(bar, 12);

    GtkWidget *lbl = gtk_label_new(info->zh ? "演示发行版:" : "Demo distro:");
    gtk_box_append(GTK_BOX(bar), lbl);

    GtkStringList *sl = gtk_string_list_new(NULL);
    gchar *cur = g_strdup_printf("%s (%s)", info->name, info->id ? info->id : "?");
    gtk_string_list_append(sl, cur);
    g_free(cur);
    for (gsize i = 0; i < G_N_ELEMENTS(demo_distros); i++)
        gtk_string_list_append(sl, demo_distros[i].pretty);

    GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(sl), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), 0);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_demo_selected), info);
    gtk_box_append(GTK_BOX(bar), dd);
    return bar;
}

static void on_demo_selected(GObject *obj, GParamSpec *ps, gpointer data)
{
    (void)ps;
    WinverInfo *info = data;
    GtkDropDown *dd = GTK_DROP_DOWN(obj);
    const guint idx = gtk_drop_down_get_selected(dd);

    if (idx == 0)
        reload_current(info);
    else
        apply_demo(info, &demo_distros[idx - 1]);

    /* update window title */
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(dd)));
    if (GTK_IS_WINDOW(root)) {
        gchar *title = g_strdup_printf(info->zh ? "关于 %s" : "About %s", info->name);
        gtk_window_set_title(GTK_WINDOW(root), title);
        g_free(title);
    }

    /* rebuild the content in place */
    GtkWidget *vbox = gtk_widget_get_parent(info->content);
    if (info->content)
        gtk_box_remove(GTK_BOX(vbox), info->content);
    info->content = build_content(info);
    gtk_box_insert_child_after(GTK_BOX(vbox), info->content, info->demo_bar);
}

static void activate(GtkApplication *app, gpointer data)
{
    WinverInfo *info = data;
    info->zh = is_zh(effective_lang());
    const gboolean zh = info->zh;

    GtkWidget *win = gtk_application_window_new(app);
    gchar *title = g_strdup_printf(zh ? "关于 %s" : "About %s", info->name);
    gtk_window_set_title(GTK_WINDOW(win), title);
    g_free(title);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    if (demo_mode) {
        info->demo_bar = make_demo_bar(info);
        gtk_box_append(GTK_BOX(vbox), info->demo_bar);
    }

    info->content = build_content(info);
    gtk_box_append(GTK_BOX(vbox), info->content);

    /* OK button (bottom right) */
    GtkWidget *ok = gtk_button_new_with_label(zh ? "确定" : "OK");
    gtk_widget_set_halign(ok, GTK_ALIGN_END);
    gtk_widget_set_margin_top(ok, 18);
    gtk_box_append(GTK_BOX(vbox), ok);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_ok_clicked), win);
    gtk_window_set_default_widget(GTK_WINDOW(win), ok);
    gtk_widget_grab_focus(ok);

    gtk_window_set_default_size(GTK_WINDOW(win), 520, -1);
    gtk_window_present(GTK_WINDOW(win));
}

static gboolean on_handle_local_options(GApplication *app, GVariantDict *options, gpointer data)
{
    (void)app;
    (void)data;
    const gchar *lang = NULL;
    if (g_variant_dict_lookup(options, "lang", "&s", &lang))
        g_strlcpy(lang_override, lang, sizeof(lang_override));
    if (g_variant_dict_contains(options, "demo"))
        demo_mode = TRUE;
    return -1; /* continue with normal startup */
}

/* generated by glib-compile-resources from data/icons.gresource.xml */
GResource *winver_icons_get_resource(void);

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    g_resources_register(winver_icons_get_resource());

    WinverInfo info = {0};
    load_os_release(&info);
    load_license(&info);

    struct utsname u;
    info.kernel = (uname(&u) == 0) ? g_strdup(u.release) : g_strdup("unknown");

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        info.username = g_strdup(pw->pw_name);
    else {
        const gchar *user = getenv("USER");
        info.username = g_strdup(user && *user ? user : "user");
    }

    GtkApplication *app = gtk_application_new("org.linux.winver", G_APPLICATION_DEFAULT_FLAGS);
    g_application_add_main_option(G_APPLICATION(app), "lang", 'l',
                                  G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
                                  "UI language override: zh, en or auto", "LANG");
    g_application_add_main_option(G_APPLICATION(app), "demo", 0,
                                  G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
                                  "Show a distro picker to demo other distributions", NULL);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(on_handle_local_options), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &info);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    g_free(info.id);
    g_free(info.name);
    g_free(info.pretty);
    g_free(info.version_id);
    g_free(info.logo);
    g_free(info.kernel);
    g_free(info.username);
    g_free(info.license_path);
    g_free(info.license_name);
    return status;
}
