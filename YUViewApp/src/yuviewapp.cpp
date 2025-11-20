/*  This file is part of YUView - The YUV player with advanced analytics toolset
*   <https://github.com/IENT/YUView>
*   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 3 of the License, or
*   (at your option) any later version.
*
*   In addition, as a special exception, the copyright holders give
*   permission to link the code of portions of this program with the
*   OpenSSL library under certain conditions as described in each
*   individual source file, and distribute linked combinations including
*   the two.
*   
*   You must obey the GNU General Public License in all respects for all
*   of the code used other than OpenSSL. If you modify file(s) with this
*   exception, you may extend this exception to your version of the
*   file(s), but you are not obligated to do so. If you do not wish to do
*   so, delete this exception statement from your version. If you delete
*   this exception statement from all source files in the program, then
*   also delete it here.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QCoreApplication>
#include <QtGlobal>
#include <QDebug>

#include <common/Typedef.h>
#include <ui/YUViewApplication.h>

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  static QMutex mutex;
  QMutexLocker lock(&mutex);

  QFile file("yuview_log.txt");
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    return;

  QTextStream out(&file);
  out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
  switch (type)
  {
  case QtDebugMsg:
    out << "Debug: ";
    break;
  case QtInfoMsg:
    out << "Info: ";
    break;
  case QtWarningMsg:
    out << "Warning: ";
    break;
  case QtCriticalMsg:
    out << "Critical: ";
    break;
  case QtFatalMsg:
    out << "Fatal: ";
    break;
  }
  out << msg << "\n";
}

int main(int argc, char *argv[])
{
  qInstallMessageHandler(myMessageOutput);

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling); // DPI support
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps); // DPI support
#endif
  QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents,false);
  QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents,false);

  qRegisterMetaType<recacheIndicator>("recacheIndicator");
  
  YUViewApplication app(argc, argv);

  return app.returnCode;
}
