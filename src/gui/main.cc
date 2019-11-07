/*
 *  Copyright (C) 2008  Alexandre Courbot
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tagaini_config.h"

#include "core/Paths.h"
#include "core/Preferences.h"
#include "core/Lang.h"
#include "core/Database.h"
#include "core/Tag.h"
#include "core/EntryListCache.h"
#include "core/Entry.h"
#include "core/EntriesCache.h"
#include "core/Plugin.h"
#include "core/jmdict/JMdictPlugin.h"
#include "core/kanjidic2/Kanjidic2Plugin.h"
//#include "core/tatoeba/TatoebaPlugin.h"
#include "gui/PreferencesWindow.h"
#include "gui/MainWindow.h"

#include "core/jmdict/JMdictPlugin.h"
#include "core/kanjidic2/Kanjidic2Plugin.h"
#include "gui/jmdict/JMdictGUIPlugin.h"
#include "gui/kanjidic2/Kanjidic2GUIPlugin.h"

// Required for exit()
#include <stdlib.h>

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QLocale>
#include <QMessageBox>
#include <QLibraryInfo>
#include <QtGlobal>

// The version must be defined by the compiler
#ifndef VERSION
#error No version defined - the -DVERSION=<version> flag must be set!
#endif

/**
 * Message handler
 */
void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
	const char *str = msg.toLatin1().constData();
	switch (type) {
	case QtDebugMsg:
		fprintf(stderr, "Debug: %s\n", str);
		break;
#if QT_VERSION >= 0x050500
	case QtInfoMsg:
		fprintf(stderr, "Info: %s\n", str);
		break;
#endif
	case QtWarningMsg:
		fprintf(stderr, "Warning: %s\n", str);
		break;
	case QtCriticalMsg:
		fprintf(stderr, "Critical: %s\n", str);
		QMessageBox::critical(0, "Critical", str);
		break;
	case QtFatalMsg:
		// TODO here a message should be emitted and caught by the main thread in
		// order to display the message box. This is because fatal errors may occur
		// in auxiliary threads too.
		//QMessageBox::critical(0, "Tagaini Jisho fatal error", msg);
		fprintf(stderr, "Fatal: %s\n", str);
		QMessageBox::critical(0, "Fatal error", QString("%1\n\nUnrecoverable error, the program will now exit.").arg(msg));
		exit(1);
	}
}

/**
 * Used to keep track of the configuration version format. This is useful
 * to update configuration options that have changed or to remove obsolete
 * ones.
 */
#define CONFIG_VERSION 7
PreferenceItem<int> configVersion("", "configVersion", 0);

void checkConfigurationVersion()
{
	// Are we running the program for the first time or updating from a previous version?
	if (configVersion.value() >= CONFIG_VERSION) return;

	QSettings settings;
	// If there is no key, then we are running the program for the first time and do not need to update.
	if (!settings.allKeys().isEmpty()) {
		switch (configVersion.value()) {
		case 0:
			settings.remove("userProfile");
			settings.remove("kanjidic/delayBetweenStrokes");
			settings.remove("kanjidic/animationSpeed"); // fallthrough
		case 1:
			settings.remove("lastUpdateCheck");
			if (settings.contains("defaultFont")) settings.setValue("mainWindow/defaultFont", settings.value("defaultFont"));
			if (settings.contains("guiLanguage")) settings.setValue("mainWindow/guiLanguage", settings.value("guiLanguage"));
			if (settings.contains("autoCheckUpdates")) settings.setValue("mainWindow/autoCheckUpdates", settings.value("autoCheckUpdates"));
			if (settings.contains("autoCheckBetaUpdates")) settings.setValue("mainWindow/autoCheckBetaUpdates", settings.value("autoCheckBetaUpdates"));
			if (settings.contains("updateCheckInterval")) settings.setValue("mainWindow/updateCheckInterval", settings.value("updateCheckInterval"));
			settings.remove("defaultFont");
			settings.remove("guiLanguage");
			settings.remove("autoCheckUpdates");
			settings.remove("autoCheckBetaUpdates");
			settings.remove("updateCheckInterval"); // fallthrough
		case 3:
			settings.remove("mainWindow/resultsView/resultsPerPage"); // fallthrough
		case 4:
			if (settings.contains("mainWindow/guiLanguage")) settings.setValue("preferredLanguages", settings.value("mainWindow/guiLanguage"));
			settings.remove("mainWindow/guiLanguage"); // fallthrough
		case 5:
			Lang::preferredDictLanguage.setValue(settings.value("preferredLanguages"));
			Lang::preferredGUILanguage.setValue(settings.value("preferredLanguages"));
			settings.remove("preferredLanguages"); // fallthrough
		case 6:
			settings.remove("entriesCacheSize"); // fallthrough
		default:
			// If we arrive here, this means we are running an pre-tracking version - do nothing in that case
			break;
		}
	}
	// Finally, update the configuration version
	configVersion.setValue(CONFIG_VERSION);
}

static void copyQt4UserDB()
{
	QFile qt4DataFile;

#if defined(Q_OS_WIN)
	// For windows, copy from the local data location to the roaming location
	qt4DataFile.setFileName(QDir(QStandardPaths::standardLocations(QStandardPaths::DataLocation)[0]).absoluteFilePath("user.db"));
#elif defined(Q_OS_UNIX)
	// Genius Qt engineers thought it would be a good idea to not provide a compatibility function, so we can just guess here...
	qt4DataFile.setFileName(Database::defaultDBFile().replace("share/Tagaini Jisho/user.db", "share/data/Tagaini Jisho/user.db"));
#else
	return;
#endif

	// Do a copy so an older version could keep working
	if (qt4DataFile.exists()) {
		qt4DataFile.copy(Database::defaultDBFile());
		QMessageBox::information(0, QCoreApplication::translate("main.cc", "Data migrated"), QCoreApplication::translate("main.cc", "Welcome to Tagaini Jisho 1.2! Your user data has successfully been migrated from Tagaini 1.0 and you should find it as it was.\n\nIf you still have an old version of Tagaini installed, please note that changes made in Tagaini 1.2 will not be visible on Tagaini 1.0 and vice-versa. If you still have your old Tagaini version, it is recommended that you uninstall it to avoid confusion."));
	}
}

/**
 * Check if a user DB directory is defined in the application settings, and
 * create a default one in case it doesn't exist.
 */
static void checkUserProfileDirectory()
{
	// Set the user profile location
	// This is done here because this function requires the QtGui module
	__userProfile = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)[0];
	// Create the user profile directory if not existing
	QDir profileDir(userProfile());
	if (!profileDir.exists()) profileDir.mkpath(".");

	// Replace the data file by the imported one if existing
	QFile dataFile(Database::defaultDBFile());
	QFile importedDataFile(Database::defaultDBFile() + ".import");
	if (importedDataFile.exists()) {
		dataFile.remove();
		importedDataFile.rename(dataFile.fileName());
		return;
	}

	// Check if we are upgrading from Qt4
	if (!dataFile.exists())
		copyQt4UserDB();
}

int main(int argc, char *argv[])
{
	extern void qt_set_sequence_auto_mnemonic(bool b);

	// Seed the random number generator
	qsrand(QDateTime::currentDateTime().toTime_t());
	QApplication app(argc, argv);

	// Enable auto-mnemonics for Mac OS X. Ideally this would only
	// be called on Mac OS X.
	qt_set_sequence_auto_mnemonic(true);

	QCoreApplication::setOrganizationDomain(__ORGANIZATION_NAME);
	QCoreApplication::setApplicationName(__APPLICATION_NAME);
	QCoreApplication::setApplicationVersion(VERSION);

	// Install the error message handler now that we have a GUI
	qInstallMessageHandler(messageHandler);

	checkConfigurationVersion();

	checkUserProfileDirectory();

	// Get the default font from the settings, if set
	if (!MainWindow::applicationFont.value().isEmpty()) {
		QFont font;
		font.fromString(MainWindow::applicationFont.value());
		app.setFont(font);
	}

	// Load translations, if available
	QString locale;
	// First check if the language is user-set
	if (!Lang::preferredGUILanguage.isDefault()) {
		locale = Lang::preferredGUILanguage.value();
	// Otherwise try the system default
	} else {
		QString name = QLocale::system().name();
#ifdef Q_OS_OSX
		// QLocale::system() ignores per-app configuration on MacOS.
		QSettings settings;
		QStringList appleLanguages = settings.value("AppleLanguages").toStringList();
		if (!appleLanguages.isEmpty())
			name = appleLanguages.first();
#endif
		locale = name.left(2);
	}
	QLocale::setDefault(QLocale(locale));
	
	// Now try to load additional translation files
	QTranslator appTranslator;
	QTranslator qtTranslator;
	// Load the translation for Tagaini
	if (appTranslator.load(lookForFile("i18n/tagainijisho_" + locale + ".qm"))) app.installTranslator(&appTranslator);
	// Load the translations for Qt
	if (qtTranslator.load(QDir(QLibraryInfo::location(QLibraryInfo::TranslationsPath)).absoluteFilePath(QString("qt_%1.qm").arg(locale))) || qtTranslator.load(lookForFile(QString("i18n/qt_%1.qm").arg(locale)))) app.installTranslator(&qtTranslator);

	// Register meta-types
	qRegisterMetaType<EntryRef>("EntryRef");
	qRegisterMetaType<EntryPointer>("EntryPointer");
	qRegisterMetaType<ConstEntryPointer>("ConstEntryPointer");
	qRegisterMetaType<QVariant>("QVariant");

	// Ensure the EntriesCache is instanciated in the main thread - that way we won't have to switch
	// threads every time an Entry is deleted
	EntriesCache::init();

	QStringList args(app.arguments());

	// Start database thread
	bool temporaryDB = false;
	QString userDBFile;
	foreach (const QString &arg, args) {
		if (arg == "--temp-db") temporaryDB = true;
		else if (arg.startsWith("--user-db=")) userDBFile = arg.mid(10);
	}
	QStringList dbErrors;
	if (!Database::init(userDBFile, temporaryDB, dbErrors)) {
		QMessageBox::critical(0, "Tagaini Jisho fatal error", dbErrors.join("<p>"));
		qFatal("All database fallbacks failed, exiting...");
	} else if (!dbErrors.empty()) {
		QMessageBox::warning(0, "Tagaini Jisho warning", dbErrors.join("<p>"));
	}

	// Initialize tags
	Tag::init();

	// Register core plugins
	Plugin *kanjidic2Plugin = new Kanjidic2Plugin();
	Plugin *jmdictPlugin = new JMdictPlugin();
	//Plugin *tatoebaPlugin = new TatoebaPlugin();
	if (!Plugin::registerPlugin(kanjidic2Plugin))
		qFatal("Error registering kanjidic2 plugin!");
	if (!Plugin::registerPlugin(jmdictPlugin))
		qFatal("Error registering JMdict plugin!");
	//if (!Plugin::registerPlugin(tatoebaPlugin))
		//qFatal("Error registering Tatoeba plugin!");

	// Create the main window
	MainWindow *mainWindow = new MainWindow();

	// Register GUI plugins
	Plugin *kanjidic2GUIPlugin = new Kanjidic2GUIPlugin();
	Plugin *jmdictGUIPlugin = new JMdictGUIPlugin();
	if (!Plugin::registerPlugin(jmdictGUIPlugin))
		qFatal("Error registering JMdict GUI plugin!");
	if (!Plugin::registerPlugin(kanjidic2GUIPlugin))
		qFatal("Error registering kanjidic2 GUI plugin!");

	mainWindow->restoreWholeState();

	// Show the main window and run the program
	mainWindow->show();
	int ret = app.exec();

	// Remove GUI plugins
	Plugin::removePlugin("kanjidic2GUI");
	Plugin::removePlugin("JMdictGUI");

	// Deleting the main window ensures there is no other DB query running in the
	// GUI thread
	delete mainWindow;

	// Remove core plugins
	//Plugin::removePlugin("Tatoeba");
	Plugin::removePlugin("JMdict");
	Plugin::removePlugin("kanjidic2");

	// Plugins must be deleted after the main window in case the latter still uses them
	// in a background thread
	delete jmdictGUIPlugin;
	delete kanjidic2GUIPlugin;
	delete jmdictPlugin;
	delete kanjidic2Plugin;

	Tag::cleanup();
	EntryListCache::cleanup();

	// Free database resources
	Database::stop();

	// Clean the entries cache
	EntriesCache::cleanup();

	return ret;
}
