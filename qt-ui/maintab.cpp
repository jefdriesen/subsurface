/*
 * maintab.cpp
 *
 * classes for the "notebook" area of the main window of Subsurface
 *
 */
#include "maintab.h"
#include "ui_maintab.h"
#include "mainwindow.h"
#include "../helpers.h"
#include "../statistics.h"
#include "../info.h"
#include "divelistview.h"
#include "modeldelegates.h"
#include "globe.h"
#include "completionmodels.h"

#include <QLabel>
#include <QCompleter>
#include <QDebug>
#include <QSet>
#include <QTableView>
#include <QSettings>
#include <QPalette>

MainTab::MainTab(QWidget *parent) : QTabWidget(parent),
				    ui(new Ui::MainTab()),
				    weightModel(new WeightModel()),
				    cylindersModel(new CylindersModel()),
				    editMode(NONE)
{
	ui->setupUi(this);
	ui->cylinders->setModel(cylindersModel);
	ui->weights->setModel(weightModel);
	ui->diveNotesMessage->hide();
	ui->diveNotesMessage->setCloseButtonVisible(false);
#ifdef __APPLE__
	setDocumentMode(false);
#else
	setDocumentMode(true);
#endif
	// we start out with the fields read-only; once things are
	// filled from a dive, they are made writeable
	ui->location->setReadOnly(true);
	ui->coordinates->setReadOnly(true);
	ui->divemaster->setReadOnly(true);
	ui->buddy->setReadOnly(true);
	ui->suit->setReadOnly(true);
	ui->notes->setReadOnly(true);
	ui->rating->setReadOnly(true);
	ui->visibility->setReadOnly(true);

	ui->editAccept->hide();
	ui->editReset->hide();

	ui->location->installEventFilter(this);
	ui->coordinates->installEventFilter(this);
	ui->divemaster->installEventFilter(this);
	ui->buddy->installEventFilter(this);
	ui->suit->installEventFilter(this);
	ui->notes->viewport()->installEventFilter(this);
	ui->rating->installEventFilter(this);
	ui->visibility->installEventFilter(this);

	QList<QObject *> statisticsTabWidgets = ui->statisticsTab->children();
	Q_FOREACH(QObject* obj, statisticsTabWidgets) {
		QLabel* label = qobject_cast<QLabel *>(obj);
		if (label)
			label->setAlignment(Qt::AlignHCenter);
	}
	ui->cylinders->setTitle(tr("Cylinders"));
	ui->cylinders->setBtnToolTip(tr("Add Cylinder"));
	connect(ui->cylinders, SIGNAL(addButtonClicked()), this, SLOT(addCylinder_clicked()));

	ui->weights->setTitle(tr("Weights"));
	ui->weights->setBtnToolTip(tr("Add Weight System"));
	connect(ui->weights, SIGNAL(addButtonClicked()), this, SLOT(addWeight_clicked()));

	connect(ui->cylinders->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editCylinderWidget(QModelIndex)));
	connect(ui->weights->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editWeigthWidget(QModelIndex)));

	ui->cylinders->view()->setItemDelegateForColumn(CylindersModel::TYPE, new TankInfoDelegate());
	ui->weights->view()->setItemDelegateForColumn(WeightModel::TYPE, new WSInfoDelegate());

	completers.buddy = new QCompleter(BuddyCompletionModel::instance(), ui->buddy);
	completers.divemaster = new QCompleter(DiveMasterCompletionModel::instance(), ui->divemaster);
	completers.location = new QCompleter(LocationCompletionModel::instance(), ui->location);
	completers.suit = new QCompleter(SuitCompletionModel::instance(), ui->suit);
	ui->buddy->setCompleter(completers.buddy);
	ui->divemaster->setCompleter(completers.divemaster);
	ui->location->setCompleter(completers.location);
	ui->suit->setCompleter(completers.suit);
}

void MainTab::enableEdition()
{
	if (ui->editAccept->isVisible() || !selected_dive)
		return;

	ui->editAccept->setChecked(true);
	ui->editAccept->show();
	ui->editReset->show();
	on_editAccept_clicked(true);
}

bool MainTab::eventFilter(QObject* object, QEvent* event)
{
	if (event->type() == QEvent::FocusIn && (object == ui->rating || object == ui->visibility)){
		enableEdition();
	}

	if (event->type() == QEvent::MouseButtonPress) {
		enableEdition();
	}
	return false; // don't "eat" the event.
}

void MainTab::clearEquipment()
{
}

void MainTab::clearInfo()
{
	ui->sacText->clear();
	ui->otuText->clear();
	ui->oxygenHeliumText->clear();
	ui->gasUsedText->clear();
	ui->dateText->clear();
	ui->diveTimeText->clear();
	ui->surfaceIntervalText->clear();
	ui->maximumDepthText->clear();
	ui->averageDepthText->clear();
	ui->waterTemperatureText->clear();
	ui->airTemperatureText->clear();
	ui->airPressureText->clear();
}

void MainTab::clearStats()
{
	ui->depthLimits->clear();
	ui->sacLimits->clear();
	ui->divesAllText->clear();
	ui->tempLimits->clear();
	ui->totalTimeAllText->clear();
	ui->timeLimits->clear();
}

#define UPDATE_TEXT(d, field)			\
	if (!d || !d->field)				\
		ui->field->setText("");			\
	else								\
		ui->field->setText(d->field)

void MainTab::updateDiveInfo(int dive)
{
	editMode = NONE;
	// This method updates ALL tabs whenever a new dive or trip is
	// selected.
	// If exactly one trip has been selected, we show the location / notes
	// for the trip in the Info tab, otherwise we show the info of the
	// selected_dive
	volume_t sacVal;
	temperature_t temp;
	struct dive *prevd;
	struct dive *d = get_dive(dive);

	process_selected_dives();
	process_all_dives(d, &prevd);

	UPDATE_TEXT(d, notes);
	UPDATE_TEXT(d, location);
	UPDATE_TEXT(d, suit);
	UPDATE_TEXT(d, divemaster);
	UPDATE_TEXT(d, buddy);

	if (d) {
		char buffer[256];
		print_gps_coordinates(buffer, sizeof buffer, d->latitude.udeg, d->longitude.udeg);
		ui->coordinates->setText(buffer);
		if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
			// only use trip relevant fields
			ui->coordinates->setVisible(false);
			ui->divemaster->setVisible(false);
			ui->DivemasterLabel->setVisible(false);
			ui->buddy->setVisible(false);
			ui->BuddyLabel->setVisible(false);
			ui->suit->setVisible(false);
			ui->SuitLabel->setVisible(false);
			ui->rating->setVisible(false);
			ui->RatingLabel->setVisible(false);
			ui->visibility->setVisible(false);
			ui->visibilityLabel->setVisible(false);
			// rename the remaining fields and fill data from selected trip
			dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
			ui->location->setReadOnly(false);
			ui->LocationLabel->setText(tr("Trip Location"));
			ui->location->setText(currentTrip->location);
			ui->notes->setReadOnly(false);
			ui->NotesLabel->setText(tr("Trip Notes"));
			ui->notes->setText(currentTrip->notes);
		} else {
			// make all the fields visible writeable
			ui->coordinates->setVisible(true);
			ui->divemaster->setVisible(true);
			ui->buddy->setVisible(true);
			ui->suit->setVisible(true);
			ui->SuitLabel->setVisible(true);
			ui->rating->setVisible(true);
			ui->RatingLabel->setVisible(true);
			ui->visibility->setVisible(true);
			ui->visibilityLabel->setVisible(true);
			ui->BuddyLabel->setVisible(true);
			ui->DivemasterLabel->setVisible(true);
			ui->divemaster->setReadOnly(false);
			ui->buddy->setReadOnly(false);
			ui->suit->setReadOnly(false);
			ui->rating->setReadOnly(false);
			ui->visibility->setReadOnly(false);
			/* and fill them from the dive */
			ui->rating->setCurrentStars(d->rating);
			ui->visibility->setCurrentStars(d->visibility);
			// reset labels in case we last displayed trip notes
			ui->location->setReadOnly(false);
			ui->coordinates->setReadOnly(false);
			ui->LocationLabel->setText(tr("Location"));
			ui->notes->setReadOnly(false);
			ui->NotesLabel->setText(tr("Notes"));
		}
		ui->maximumDepthText->setText(get_depth_string(d->maxdepth, TRUE));
		ui->averageDepthText->setText(get_depth_string(d->meandepth, TRUE));
		ui->otuText->setText(QString("%1").arg(d->otu));
		ui->waterTemperatureText->setText(get_temperature_string(d->watertemp, TRUE));
		ui->airTemperatureText->setText(get_temperature_string(d->airtemp, TRUE));
		ui->gasUsedText->setText(get_volume_string(get_gas_used(d), TRUE));
		ui->oxygenHeliumText->setText(get_gaslist(d));
		ui->dateText->setText(get_short_dive_date_string(d->when));
		ui->diveTimeText->setText(QString::number((int)((d->duration.seconds + 30) / 60)));
		if (prevd)
			ui->surfaceIntervalText->setText(get_time_string(d->when - (prevd->when + prevd->duration.seconds), 4));
		if ((sacVal.mliter = d->sac) > 0)
			ui->sacText->setText(get_volume_string(sacVal, TRUE).append(tr("/min")));
		else
			ui->sacText->clear();
		if (d->surface_pressure.mbar)
			/* this is ALWAYS displayed in mbar */
			ui->airPressureText->setText(QString("%1mbar").arg(d->surface_pressure.mbar));
		else
			ui->airPressureText->clear();
		ui->depthLimits->setMaximum(get_depth_string(stats_selection.max_depth, TRUE));
		ui->depthLimits->setMinimum(get_depth_string(stats_selection.min_depth, TRUE));
		ui->depthLimits->setAverage(get_depth_string(stats_selection.avg_depth, TRUE));
		ui->sacLimits->setMaximum(get_volume_string(stats_selection.max_sac, TRUE).append(tr("/min")));
		ui->sacLimits->setMinimum(get_volume_string(stats_selection.min_sac, TRUE).append(tr("/min")));
		ui->sacLimits->setAverage(get_volume_string(stats_selection.avg_sac, TRUE).append(tr("/min")));
		ui->divesAllText->setText(QString::number(stats_selection.selection_size));
		temp.mkelvin = stats_selection.max_temp;
		ui->tempLimits->setMaximum(get_temperature_string(temp, TRUE));
		temp.mkelvin = stats_selection.min_temp;
		ui->tempLimits->setMinimum(get_temperature_string(temp, TRUE));
		if (stats_selection.combined_temp && stats_selection.combined_count) {
			const char *unit;
			get_temp_units(0, &unit);
			ui->tempLimits->setAverage(QString("%1%2").arg(stats_selection.combined_temp / stats_selection.combined_count, 0, 'f', 1).arg(unit));
		}
		ui->totalTimeAllText->setText(get_time_string(stats_selection.total_time.seconds, 0));
		int seconds = stats_selection.total_time.seconds;
		if (stats_selection.selection_size)
			seconds /= stats_selection.selection_size;
		ui->timeLimits->setAverage(get_time_string(seconds, 0));
		ui->timeLimits->setMaximum(get_time_string(stats_selection.longest_time.seconds, 0));
		ui->timeLimits->setMinimum(get_time_string(stats_selection.shortest_time.seconds, 0));
		cylindersModel->setDive(d);
		weightModel->setDive(d);
	} else {
		/* make the fields read-only */
		ui->location->setReadOnly(true);
		ui->coordinates->setReadOnly(true);
		ui->divemaster->setReadOnly(true);
		ui->buddy->setReadOnly(true);
		ui->suit->setReadOnly(true);
		ui->notes->setReadOnly(true);
		ui->rating->setReadOnly(true);
		ui->visibility->setReadOnly(true);
		/* clear the fields */
		ui->rating->setCurrentStars(0);
		ui->sacText->clear();
		ui->otuText->clear();
		ui->oxygenHeliumText->clear();
		ui->dateText->clear();
		ui->diveTimeText->clear();
		ui->surfaceIntervalText->clear();
		ui->maximumDepthText->clear();
		ui->averageDepthText->clear();
		ui->visibility->setCurrentStars(0);
		ui->waterTemperatureText->clear();
		ui->airTemperatureText->clear();
		ui->gasUsedText->clear();
		ui->airPressureText->clear();
		cylindersModel->clear();
		weightModel->clear();
		ui->depthLimits->clear();
		ui->sacLimits->clear();
		ui->divesAllText->clear();
		ui->tempLimits->clear();
		ui->totalTimeAllText->clear();
		ui->timeLimits->clear();
	}
}

void MainTab::addCylinder_clicked()
{
	cylindersModel->add();
	mark_divelist_changed(TRUE);
}

void MainTab::addWeight_clicked()
{
	weightModel->add();
	mark_divelist_changed(TRUE);
}

void MainTab::reload()
{
	SuitCompletionModel::instance()->updateModel();
	BuddyCompletionModel::instance()->updateModel();
	LocationCompletionModel::instance()->updateModel();
	DiveMasterCompletionModel::instance()->updateModel();
}

void MainTab::on_editAccept_clicked(bool edit)
{
	ui->location->setReadOnly(!edit);
	ui->coordinates->setReadOnly(!edit);
	ui->divemaster->setReadOnly(!edit);
	ui->buddy->setReadOnly(!edit);
	ui->suit->setReadOnly(!edit);
	ui->notes->setReadOnly(!edit);
	ui->rating->setReadOnly(!edit);
	ui->visibility->setReadOnly(!edit);

	mainWindow()->dive_list()->setEnabled(!edit);

	if (edit) {

		// We may be editing one or more dives here. backup everything.
		notesBackup.clear();

		if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
			// we are editing trip location and notes
			ui->diveNotesMessage->setText(tr("This trip is being edited. Select Save or Undo when ready."));
			ui->diveNotesMessage->animatedShow();
			notesBackup[NULL].notes = ui->notes->toPlainText();
			notesBackup[NULL].location = ui->location->text();
			editMode = TRIP;
		} else {
			ui->diveNotesMessage->setText(tr("This dive is being edited. Select Save or Undo when ready."));
			ui->diveNotesMessage->animatedShow();

			// We may be editing one or more dives here. backup everything.
			struct dive *mydive;
			for (int i = 0; i < dive_table.nr; i++) {
				mydive = get_dive(i);
				if (!mydive)
					continue;
				if (!mydive->selected)
					continue;

				notesBackup[mydive].buddy = QString(mydive->buddy);
				notesBackup[mydive].suit = QString(mydive->suit);
				notesBackup[mydive].notes = QString(mydive->notes);
				notesBackup[mydive].divemaster = QString(mydive->divemaster);
				notesBackup[mydive].location = QString(mydive->location);
				notesBackup[mydive].rating = mydive->rating;
				notesBackup[mydive].visibility = mydive->visibility;
				notesBackup[mydive].latitude = mydive->latitude;
				notesBackup[mydive].longitude = mydive->longitude;
				notesBackup[mydive].coordinates  = ui->location->text();
			}
		editMode = DIVE;
		}
	} else {
		ui->diveNotesMessage->animatedHide();
		ui->editAccept->hide();
		ui->editReset->hide();
		/* now figure out if things have changed */
		if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
			if (notesBackup[NULL].notes != ui->notes->toPlainText() ||
			    notesBackup[NULL].location != ui->location->text())
				mark_divelist_changed(TRUE);
		} else {
			struct dive *curr = current_dive;
			if (notesBackup[curr].buddy != ui->buddy->text() ||
			    notesBackup[curr].suit != ui->suit->text() ||
			    notesBackup[curr].notes != ui->notes->toPlainText() ||
			    notesBackup[curr].divemaster != ui->divemaster->text() ||
			    notesBackup[curr].location  != ui->location->text() ||
			    notesBackup[curr].coordinates != ui->coordinates->text() ||
			    notesBackup[curr].rating 	!= ui->visibility->currentStars() ||
			    notesBackup[curr].visibility != ui->rating->currentStars())

				mark_divelist_changed(TRUE);
			if (notesBackup[curr].location != ui->location->text() ||
			    notesBackup[curr].coordinates != ui->coordinates->text()) {
				mainWindow()->globe()->reload();
				mainWindow()->globe()->centerOn(current_dive);
			}
		}
		editMode = NONE;
	}
	QPalette p;
	ui->buddy->setPalette(p);
	ui->notes->setPalette(p);
	ui->location->setPalette(p);
	ui->divemaster->setPalette(p);
	ui->suit->setPalette(p);
}

#define EDIT_TEXT2(what, text) \
	textByteArray = text.toLocal8Bit(); \
	free(what);\
	what = strdup(textByteArray.data());

#define EDIT_TEXT(what, text) \
	QByteArray textByteArray = text.toLocal8Bit(); \
	free(what);\
	what = strdup(textByteArray.data());

void MainTab::on_editReset_clicked()
{
	if (!ui->editAccept->isChecked())
		return;

	if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1){
		ui->notes->setText(notesBackup[NULL].notes );
		ui->location->setText(notesBackup[NULL].location);
	}else{
		struct dive *curr = current_dive;
		ui->notes->setText(notesBackup[curr].notes );
		ui->location->setText(notesBackup[curr].location);
		ui->coordinates->setText(notesBackup[curr].coordinates);
		ui->buddy->setText(notesBackup[curr].buddy);
		ui->suit->setText(notesBackup[curr].suit);
		ui->divemaster->setText(notesBackup[curr].divemaster);
		ui->rating->setCurrentStars(notesBackup[curr].rating);
		ui->visibility->setCurrentStars(notesBackup[curr].visibility);

		struct dive *mydive;
		for (int i = 0; i < dive_table.nr; i++) {
			mydive = get_dive(i);
			if (!mydive)
				continue;
			if (!mydive->selected)
				continue;

			QByteArray textByteArray;
			EDIT_TEXT2(mydive->buddy, notesBackup[mydive].buddy);
			EDIT_TEXT2(mydive->suit, notesBackup[mydive].suit);
			EDIT_TEXT2(mydive->notes, notesBackup[mydive].notes);
			EDIT_TEXT2(mydive->divemaster, notesBackup[mydive].divemaster);
			EDIT_TEXT2(mydive->location, notesBackup[mydive].location);
			mydive->latitude = notesBackup[mydive].latitude;
			mydive->longitude = notesBackup[mydive].longitude;
			mydive->rating = notesBackup[mydive].rating;
			mydive->visibility = notesBackup[mydive].visibility;
		}
	}
	ui->editAccept->setChecked(false);
	ui->diveNotesMessage->animatedHide();

	ui->location->setReadOnly(true);
	ui->coordinates->setReadOnly(true);
	ui->divemaster->setReadOnly(true);
	ui->buddy->setReadOnly(true);
	ui->suit->setReadOnly(true);
	ui->notes->setReadOnly(true);
	ui->rating->setReadOnly(true);
	ui->visibility->setReadOnly(true);
	mainWindow()->dive_list()->setEnabled(true);

	ui->editAccept->hide();
	ui->editReset->hide();
	notesBackup.clear();
	QPalette p;
	ui->buddy->setPalette(p);
	ui->notes->setPalette(p);
	ui->location->setPalette(p);
	ui->divemaster->setPalette(p);
	ui->suit->setPalette(p);
	editMode = NONE;
}
#undef EDIT_TEXT2

#define EDIT_SELECTED_DIVES( WHAT ) \
	if (editMode == NONE) \
		return; \
\
	for (int i = 0; i < dive_table.nr; i++) { \
		struct dive *mydive = get_dive(i); \
		if (!mydive) \
			continue; \
		if (!mydive->selected) \
			continue; \
\
		WHAT; \
	}

void markChangedWidget(QWidget *w){
	QPalette p;
	p.setBrush(QPalette::Base, QColor(Qt::yellow).lighter());
	w->setPalette(p);
}

void MainTab::on_buddy_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->buddy, text) );
	markChangedWidget(ui->buddy);
}

void MainTab::on_divemaster_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->divemaster, text) );
	markChangedWidget(ui->divemaster);
}

void MainTab::on_location_textChanged(const QString& text)
{
	if (editMode == NONE)
		return;
	if (editMode == TRIP && mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		// we are editing a trip
		dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
		EDIT_TEXT(currentTrip->location, text);
	} else if (editMode == DIVE){
		struct dive* dive;
		int i = 0;
		for_each_dive(i, dive){
			QString location(dive->location);
			if (location == text &&
					(dive->latitude.udeg || dive->longitude.udeg)){
				EDIT_SELECTED_DIVES( mydive->latitude = dive->latitude )
				EDIT_SELECTED_DIVES( mydive->longitude = dive->longitude )
				char buffer[256];
				print_gps_coordinates(buffer, sizeof buffer, dive->latitude.udeg, dive->longitude.udeg);
				ui->coordinates->setText(buffer);
				break;
			}
		}
		EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->location, text) )
	}

	markChangedWidget(ui->location);
}

void MainTab::on_suit_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->suit, text) );
	markChangedWidget(ui->suit);
}

void MainTab::on_notes_textChanged()
{
	if (editMode == NONE)
		return;
	if (editMode == TRIP && mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		// we are editing a trip
		dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
		EDIT_TEXT(currentTrip->notes, ui->notes->toPlainText());
	} else if (editMode == DIVE) {
		EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->notes, ui->notes->toPlainText()) );
	}
	markChangedWidget(ui->notes);
}

#undef EDIT_TEXT

void MainTab::on_coordinates_textChanged(const QString& text)
{
	QByteArray textByteArray = text.toLocal8Bit();
	EDIT_SELECTED_DIVES(gps_changed(mydive, NULL, textByteArray.data()));
}

void MainTab::on_rating_valueChanged(int value)
{
	EDIT_SELECTED_DIVES(mydive->rating  = value );
}

void MainTab::on_visibility_valueChanged(int value)
{
	EDIT_SELECTED_DIVES( mydive->visibility = value );
}

void MainTab::editCylinderWidget(const QModelIndex& index)
{
	if (index.isValid() && index.column() != CylindersModel::REMOVE)
		ui->cylinders->edit(index);
}

void MainTab::editWeigthWidget(const QModelIndex& index)
{
	if (index.isValid() && index.column() != WeightModel::REMOVE)
		ui->weights->edit(index);
}
