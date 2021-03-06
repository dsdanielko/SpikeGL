/*
 *  Bug_ConfigDialog.cpp
 *  SpikeGL
 *
 *  Created by calin on 1/29/15.
 *  Copyright 2015 Calin Culianu <calin.culianu@gmail.com>. All rights reserved.
 *
 */

#include <QDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QPropertyAnimation>
#include "Bug_ConfigDialog.h"
#include "Util.h"
#include "MainApp.h"
#include "ConfigureDialogController.h"

Bug_ConfigDialog::Bug_ConfigDialog(DAQ::Params & p, QObject *parent) : QObject(parent), acceptedParams(p)
{
    dialogW = new QDialog(0);
    dialogW->setAttribute(Qt::WA_DeleteOnClose, false);
	dialog = new Ui::Bug_ConfigDialog;
    dialog->setupUi(dialogW);    
    aoPassThru = new Ui::AoPassThru;
    aoPassThru->setupUi(dialog->aoPassthruWidget);
    aoPassThru->aoPassthruLabel->hide();
    aoPassThru->aoPassthruLE->hide();
    aoPassThru->aoNoteLink->hide();
    aoPassThru->aoPassthruGB->setGeometry(aoPassThru->aoPassthruGB->x(),aoPassThru->aoPassthruGB->y(),dialog->aoPassthruWidget->width(),120);
    dialog->aoPassthruWidget->setGeometry(dialog->aoPassthruWidget->x(),dialog->aoPassthruWidget->y(),dialog->aoPassthruWidget->width(),120);
    aoPassThru->aoPassthruGB->setToolTip("If enabled, then one of the Bug3 incoming channels can be sent to an NI-DAQ AO device in real-time.");
    Connect(aoPassThru->bufferSizeSlider, SIGNAL(valueChanged(int)), this, SLOT(aoBufferSizeSliderChanged()));
    Connect(aoPassThru->aoDeviceCB, SIGNAL(activated(const QString &)), this, SLOT(aoDeviceCBChanged()));
    Connect(dialog->ttlTrigCB, SIGNAL(currentIndexChanged(int)), this, SLOT(ttlTrigCBChanged()));
	Connect(dialog->browseBut, SIGNAL(clicked()), this, SLOT(browseButClicked()));
    Connect(dialog->ttlAltChk, SIGNAL(toggled(bool)), this, SLOT(ttlAltClicked(bool)));

	for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i) {
		ttls[i] = new QCheckBox(QString::number(i), dialog->ttlW);
		dialog->ttlLayout->addWidget(ttls[i]);
	}
    extraAIW = new QGroupBox("Bug Extra AI Params", dialogW);
    extraAI = new Ui::Bug_ExtraAIParams;
    extraAI->setupUi(extraAIW);
    extraAIW->resize(272,/*115*/167); extraAIW->move(33,208);
    Connect(extraAI->chan1, SIGNAL(activated(int)), this, SLOT(ttlTrigCBChanged()));
    Connect(extraAI->trigBackupChk, SIGNAL(toggled(bool)), this, SLOT(ttlTrigCBChanged()));
    Connect(extraAI->chan2, SIGNAL(activated(int)), this, SLOT(ttlTrigCBChanged()));
}

Bug_ConfigDialog::~Bug_ConfigDialog()
{
    for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i) delete ttls[i], ttls[i] = 0;
    delete dialogW; dialogW = 0;
    delete dialog; dialog = 0;
    delete extraAI; extraAI = 0;
}

void Bug_ConfigDialog::browseButClicked()
{
    QString fn = QFileDialog::getSaveFileName(dialogW, "Select output file", dialog->outputFileLE->text());
    if (fn.length()) {
		QFileInfo fi(fn);
		QString suff = fi.suffix();
		if (!suff.startsWith(".")) suff = QString(".") + suff;
		if (suff.toLower() != ".bin") fn += ".bin";
		dialog->outputFileLE->setText(fn);
	}
}

int Bug_ConfigDialog::exec() 
{
	mainApp()->configureDialogController()->loadSettings(); // this makes the shared params object get updated form the settings

	guiFromSettings();

	ValidationResult vr;
	int r;
	
	do {
		vr = ABORT;
		r = dialogW->exec();
		QString errTit, errMsg;
		if (r == QDialog::Accepted) {
			vr = validateForm(errTit, errMsg);
			if (vr == OK) {
				DAQ::Params & p(acceptedParams);
				p.fg.reset();
				p.bug.reset();
				p.bug.enabled = true;
                p.bug.graphBadData = dialog->graphBad->isChecked();
				p.bug.rate = dialog->acqRateCB->currentIndex();
                if (dialog->ttlTrigCB->currentIndex() >= 1 + DAQ::BugTask::TotalAuxChans+DAQ::BugTask::TotalTTLChans) {
                    p.bug.aiTrig = dialog->ttlTrigCB->currentText();
                } else if (dialog->ttlTrigCB->currentIndex() > 2) {
                    p.bug.ttlTrig = dialog->ttlTrigCB->currentIndex()-3;
                    p.bug.whichTTLs |= (0x1 << p.bug.ttlTrig);
                } else if (dialog->ttlTrigCB->currentIndex() > 0) {
                    p.bug.auxTrig = dialog->ttlTrigCB->currentIndex()-1;
                }
                p.bug.altTTL = dialog->ttlAltChk->isChecked();
				for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i) {
					if (ttls[i]->isChecked()) p.bug.whichTTLs |= 0x1 << i;  
				}
                const bool hasAuxTrig = p.bug.auxTrig > -1, hasTtlTrig = p.bug.ttlTrig > -1, hasAITrig = p.bug.aiTrig.length()!=0;
                p.bug.aiChans.clear();
                if (extraAI->chan1->currentIndex() != 0) p.bug.aiChans.push_back(extraAI->chan1->currentText());
                if (extraAI->chan2->currentIndex() != 0) p.bug.aiChans.push_back(extraAI->chan2->currentText());
                const bool hasAIChans = !p.bug.aiChans.isEmpty();
                int trigIdx=0;
                if (hasAuxTrig)
                    trigIdx = DAQ::BugTask::FirstAuxChan;
                else if (hasTtlTrig)
                    trigIdx = DAQ::BugTask::BaseNChans;
                else if (hasAITrig)
                    trigIdx = DAQ::BugTask::BaseNChans;
                int numTtls = 0;
                for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i)
                    if (p.bug.whichTTLs & (0x1<<i)) ++numTtls;
                if (!hasAuxTrig && (hasTtlTrig || hasAITrig)) {
                    for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i) {
                        if (i == p.bug.ttlTrig) break;
                        // for both TTL trig mode and AI trig mode, this here gets incremented once for each TTL line turned on!
                        if (p.bug.whichTTLs & (0x1<<i)) ++trigIdx;
                    }
                } else if (hasAuxTrig) {
                    trigIdx += p.bug.auxTrig;
                }

                if ((hasAuxTrig || hasTtlTrig)
                        && p.bug.aiChans.count() && extraAI->trigBackupChk->isEnabled()
                        && extraAI->trigBackupChk->isChecked()) {
                    p.bug.backupTrigger = DAQ::BugTask::BaseNChans + numTtls;
                    p.bug.aithold = extraAI->thresh->value();
                }

				p.bug.clockEdge = dialog->clkEdgeCB->currentIndex();
				p.bug.hpf = dialog->hpfChk->isChecked() ? dialog->hpfSB->value(): 0;
				p.bug.snf = dialog->notchFilterChk->isChecked();
				p.bug.errTol = dialog->errTolSB->value();
				p.suppressGraphs = dialog->disableGraphsChk->isChecked();
				p.resumeGraphSettings = dialog->resumeGraphSettingsChk->isChecked();
				
				int nttls = 0;
				for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i)
					if ( (p.bug.whichTTLs >> i) & 0x1) ++nttls; // count number of ttls set 
                p.nVAIChans = DAQ::BugTask::BaseNChans + nttls + p.bug.aiChans.count() + (p.bug.graphBadData?1:0);
				p.nVAIChans1 = p.nVAIChans;
				p.nVAIChans2 = 0;
				p.aiChannels2.clear();
                //p.aiString2.clear();
				p.aiChannels.resize(p.nVAIChans);
				p.subsetString = dialog->channelSubsetLE->text();
				p.demuxedBitMap.resize(p.nVAIChans); p.demuxedBitMap.fill(true);
                for (int i = 0; i < (int)p.nVAIChans; ++i) {
					p.aiChannels[i] = i;
				}
				if (p.subsetString.compare("ALL", Qt::CaseInsensitive) != 0) {
					QVector<unsigned> subsetChans;
					bool err;
					p.subsetString = ConfigureDialogController::parseAIChanString(p.subsetString, subsetChans, &err, true);
					if (!err) {
						p.demuxedBitMap.fill(false);
						for (int i = 0; i < subsetChans.size(); ++i) {
							int bit = subsetChans[i];
							if (bit < p.demuxedBitMap.size()) p.demuxedBitMap[bit] = true;
						}
						if (p.demuxedBitMap.count(true) == 0) {
							Warning() << "Bug3 channel subset string specified invalid. Proceeding with 'ALL' channels set to save!";
							p.demuxedBitMap.fill(true);
							p.subsetString = "ALL";
						}
					}
				}
				
				p.overrideGraphsPerTab = dialog->graphsPerTabCB->currentText().toUInt();

				p.isIndefinite = true;
				p.isImmediate = true;
				p.acqStartEndMode = DAQ::Immediate;
				p.usePD = 0;
				p.chanMap = ChanMap();
                if (hasAuxTrig || hasTtlTrig || hasAITrig) {
					p.acqStartEndMode = DAQ::Bug3TTLTriggered;
                    p.overrideGraphsPerTab = 36; ///< force all graphs to always be on screen!
					p.isImmediate = false;
                    p.idxOfPdChan = trigIdx;
					p.usePD = true;
					p.pdChanIsVirtual = true;
                    p.pdChan = trigIdx;
                    p.bug.trigThreshV = p.bug.altTTL ? dialog->trigAltThreshSB->value() : dialog->trigThreshSB->value();
                    p.pdThresh = 10000; // in sample vals.. high TTL line is always above 0 in sample vals.. so 10000 is safe NOTE: we get this from GUI in the ugly RANGE code a few dozen lines below!!!
                    p.pdThreshW = p.bug.altTTL ? static_cast<unsigned>(dialog->trigWSB_2->value()) : static_cast<unsigned>(dialog->trigWSB->value());
					p.pdPassThruToAO = -1;
                    p.pdStopTime = p.bug.altTTL ? dialog->trigPost->value()/1000.0 : dialog->trigStopTimeSB->value();
                    p.silenceBeforePD = p.bug.altTTL ? dialog->trigPre_2->value()/1000.0 : dialog->trigPre->value()/1000.0;                   
                }

                DAQ::Range aiRangeToUse(-5.,5.);
                if (hasAIChans) {
                    p.bug.aiDownsampleFactor = 1.0 / qPow(2.0,double(extraAI->rate->currentIndex()));
                    {
                        QString rstr = extraAI->rngCB->currentText();
                        if (rstr.length() && 2==::sscanf(rstr.toUtf8().constData(), "%lf-%lf V", &aiRangeToUse.min, &aiRangeToUse.max))
                        {
                            // ok..
//                            Debug() << "Using AI range: " << aiRangeToUse.min << "," << aiRangeToUse.max;
                            p.bug.guiAIRange = rstr;
                        } else {
                            Warning() << "Failed to parse range string from UI: " << rstr;
                            aiRangeToUse = DAQ::Range(-5.0,5.0);
                        }
                    }
                    if (p.bug.aiChans.count() == 2) {
                        const QString c1(p.bug.aiChans.front()), c2(p.bug.aiChans.back());
                        errTit = errMsg = "";
                        if (c1.compare(c2,Qt::CaseInsensitive) == 0) {
                            bool isTrig = p.bug.aiTrig.compare(c1,Qt::CaseInsensitive) == 0;
                            errTit = "Invalid AI Specification";
                            errMsg = QString("Specified the same AI channel twice") + (isTrig ? ", once as trigger and then as second AI" : "");
                        }
                        QStringList s1 = c1.split("/"), s2 = c2.split("/");
                        if (s1.count() && s2.count() && s1.front().compare(s2.front(),Qt::CaseInsensitive) != 0) {
                            errTit = "Invalid AI Specification";
                            errMsg = "The two AI channels specified in the 'Extra AI' box must both be physically on the same NI-DAQ Device!";
                        }
                        if (errTit.length()) {
                            QMessageBox::critical(dialogW,errTit,errMsg,QMessageBox::Ok);
                            vr = AGAIN;
                            continue;
                        }
                    }
                }

				if (AGAIN == ConfigureDialogController::setFilenameTakingIntoAccountIncrementHack(p, p.acqStartEndMode, dialog->outputFileLE->text(), dialogW)) {
					vr = AGAIN;
					continue;
				}
				
                if ( (p.aoPassthru = (aoPassThru->aoDeviceCB->count() && aoPassThru->aoPassthruGB->isChecked())) ) {
                    p.aoDev = mainApp()->configureDialogController()->getAODevName(aoPassThru);
                    p.bug.aoSrate = aoPassThru->srateSB->value();
                    p.aoClock = aoPassThru->aoClockCB->currentText();
                    QStringList rngs = aoPassThru->aoRangeCB->currentText().split(" - ");
                    if (rngs.count() != 2) {
                        errTit = "AO Range ComboBox invalid!";
                        errMsg = "INTERNAL ERROR: AO Range ComboBox needs numbers of the form REAL - REAL!";
                        Error() << errMsg;
                        vr=ABORT;
                        continue;
                    }
                    p.aoRange.min = rngs.first().toDouble();
                    p.aoRange.max = rngs.last().toDouble();
                    p.aoBufferSizeCS = aoPassThru->bufferSizeSlider->value();
                    if (p.aoBufferSizeCS > 100) p.aoBufferSizeCS = 100;
                    else if (p.aoBufferSizeCS <= 0) p.aoBufferSizeCS = 1;
                    bool errflg;
                    QMap<unsigned, unsigned> m = ConfigureDialogController::parseAOPassthruString(p.bug.aoPassthruString, &errflg);
                    unsigned k = 0, v = 0;
                    if (!errflg && !m.empty()) {
                        k = m.begin().key();
                        v = m.begin().value();
                    }
                    if (k >= DAQ::GetNAOChans(p.aoDev) || v >= p.nVAIChans) k = 0, v = 0;
                    p.aoChannels = QVector<unsigned>(1,k);
                    p.aoPassthruMap.clear();
                    p.aoPassthruMap.insert(k,v);
                    p.aoPassthruString = p.bug.aoPassthruString = QString("%1=%2").arg(k).arg(v);
                }
                p.aiTerm = DAQ::StringToTermConfig(extraAI->terminationCB->currentText());

				saveSettings();

                // this stuff doesn't need to be saved since it's constant and will mess up regular acq "remembered" values
                p.aoSrate = p.bug.aoSrate; // fudged.. needed because AOWriteThread::run looks at this variable but we don't want to save it above in saveSettings()...
				p.dev = "USB_Bug3";
				p.nExtraChans1 = 0;
				p.nExtraChans2 = 0;
				
				p.extClock = true;
				p.mode = DAQ::AIRegular;
				p.dualDevMode = false;
				p.stimGlTrigResave = false;
				p.srate = DAQ::BugTask::SamplingRate;
                p.aiString = QString("0:%1").arg(p.nVAIChans-1);
				p.customRanges.resize(p.nVAIChans);
				p.chanDisplayNames.resize(p.nVAIChans);
				DAQ::Range rminmax(1e9,-1e9);
				for (unsigned i = 0; i < p.nVAIChans; ++i) {
					DAQ::Range r;
                    int chan_id_for_display = i;
					if (i < unsigned(DAQ::BugTask::TotalNeuralChans)) { // NEU
						r.min = -(2.3*2048/2.0)/1e6 /*-2.4/1e3*/, r.max = 2.3*2048/2.0/1e6/*2.4/1e3*/;
					} else if (i < (unsigned)DAQ::BugTask::TotalNeuralChans+DAQ::BugTask::TotalEMGChans) { //EMG
						r.min = -(23.0*2048)/2.0/1e6 /*-24.0/1e3*/, r.max = (23.0*2048)/2.0/1e6/*24.0/1e3*/;
                    } else if (i < (unsigned)DAQ::BugTask::TotalNeuralChans+DAQ::BugTask::TotalEMGChans+DAQ::BugTask::TotalAuxChans) { // AUX
						//r.min = 1.2495+.62475, r.max = 1.2495*2;				
						//r.min = -(2.4*2048)/2./1e3, r.max = (2.4*2048)/2./1e3;
						r.min = DAQ::BugTask::ADCStep*1024., r.max = DAQ::BugTask::ADCStep*1024.+DAQ::BugTask::ADCStep*2048.;
                        if (hasAuxTrig && trigIdx == (int)i) {
                            // acq is using AUX line for triggering.. convert Volts from GUI to a sample value for MainApp:taskReadFunc()
                            int samp = static_cast<int>(( ( (p.bug.trigThreshV-r.min)/(r.max-r.min) ) * 65535.0 ) - 32768.0);
                            if (samp < -32768) samp = -32768;
                            if (samp > 32767) samp = 32767;
                            p.pdThresh = static_cast<int16>(samp);
                        }
                    } else { // ttl lines AND AI lines AND "Graph Missing" graph
						r.min = -5., r.max = 5.;
                        // since ttl lines may be missing in channel set, renumber the ones that are missing for display purposes
                        int lim = (int(i)-int(DAQ::BugTask::TotalNeuralChans+DAQ::BugTask::TotalEMGChans+DAQ::BugTask::TotalAuxChans))+1;
                        for (int j=0; j < lim && j < DAQ::BugTask::TotalTTLChans ; ++j)
                            if (!(p.bug.whichTTLs & (0x1<<j))) ++chan_id_for_display, ++lim;

                        bool isbak = false;
                        if ( DAQ::BugTask::isAIChan(p,i))
                            r=aiRangeToUse;

                        if ( ((hasTtlTrig || hasAITrig) && trigIdx == int(i))
                             || (isbak=((hasTtlTrig || hasAuxTrig) && p.bug.backupTrigger == int(i))) ) {
                            // acq is using TTL line for triggering.. convert Volts from GUI to a sample value for MainApp:taskReadFunc().. note this is ill defined really as TTL lines are always either 0 or 5V
                            int samp = static_cast<int>(( ( ( (isbak?p.bug.aithold : p.bug.trigThreshV)-r.min)/(r.max-r.min) ) * 65535.0 ) - 32768.0);
                            if (samp < -32768) samp = -32768;
                            if (samp > 32767) samp = 32767;
                            if (isbak) p.bug.backupTriggerThresh = samp;
                            else p.pdThresh = static_cast<int16>(samp);
                        } else if (p.bug.graphBadData && i+1 == p.nVAIChans) {
                            // bad data graph.. set range appropriately
                            r.min=-1., r.max = 1.;
                        }
                    }
					if (rminmax.min > r.min) rminmax.min = r.min;
					if (rminmax.max < r.max) rminmax.max = r.max;
					p.customRanges[i] = r;
                    p.chanDisplayNames[i] = DAQ::BugTask::getChannelName(chan_id_for_display, p);
				}

                //Debug() << " triggerIndex=" << p.idxOfPdChan << " triggerThresh=" << p.pdThresh << " backupIndex=" << p.bug.backupTrigger << " backupThresh=" << p.bug.backupTriggerThresh;

				p.range = rminmax;
				p.auxGain = 1.0;

//                if (hasAuxTrig || hasTtlTrig) Debug() << "p.pdThresh=" << p.pdThresh << " p.pdChan=" << p.pdChan << " p.idxOfPdChan=" << p.idxOfPdChan;
				
			} else if (vr==AGAIN) {
				if (errTit.length() && errMsg.length())
					QMessageBox::critical(dialogW, errTit, errMsg);
			} else if (vr==ABORT) r = QDialog::Rejected;
		}
	} while (vr==AGAIN && r==QDialog::Accepted);	

	return r;
}

void Bug_ConfigDialog::guiFromSettings()
{
	DAQ::Params & p(acceptedParams);
    dialog->graphBad->setChecked(p.bug.graphBadData);
	dialog->outputFileLE->setText(p.outputFile);
	dialog->channelSubsetLE->setText(p.subsetString);
	for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i) {
		ttls[i]->setChecked(p.bug.whichTTLs & (0x1<<i));
	}

    int ci = extraAI->terminationCB->findText(DAQ::TermConfigToString(p.aiTerm), Qt::MatchExactly);
    extraAI->terminationCB->setCurrentIndex(ci > -1 ? ci : 0);

    // populate trigger cb with list of all AI channels
    const int ai_trig_offset = DAQ::BugTask::TotalAuxChans+DAQ::BugTask::TotalTTLChans+1;
    int selected_ai_cb_ix = -1;
    DAQ::DeviceChanMap cm = DAQ::ProbeAllAIChannels();
    if (dialog->ttlTrigCB->count() <= ai_trig_offset) {
        for (DAQ::DeviceChanMap::iterator it = cm.begin(); it != cm.end(); ++it) {
            for (QStringList::iterator it2 = it.value().begin(); it2 != it.value().end(); ++it2) {
                dialog->ttlTrigCB->addItem(*it2);
            }
        }
    }
    // populate 'extra' cb with list of all AI channels
    if (extraAI->chan1->count() == 1) {
        for (DAQ::DeviceChanMap::iterator it = cm.begin(); it != cm.end(); ++it) {
            for (QStringList::iterator it2 = it.value().begin(); it2 != it.value().end(); ++it2) {
               extraAI->chan1->addItem(*it2);
               if (p.bug.aiChans.count() && p.bug.aiChans.front().compare(*it2) == 0) extraAI->chan1->setCurrentIndex(extraAI->chan1->count()-1);
            }
        }
    }
    if (extraAI->chan2->count() == 1) {
        for (DAQ::DeviceChanMap::iterator it = cm.begin(); it != cm.end(); ++it) {
            for (QStringList::iterator it2 = it.value().begin(); it2 != it.value().end(); ++it2) {
               extraAI->chan2->addItem(*it2);
               if (p.bug.aiChans.count() == 2 && p.bug.aiChans.back().compare(*it2) == 0) extraAI->chan2->setCurrentIndex(extraAI->chan2->count()-1);
            }
        }
    }
    extraAI->trigBackupChk->setChecked(p.bug.backupTrigger > -1);
    extraAI->thresh->setValue(p.bug.aithold);

    // set the sampling rate cb in the extra ai submenu
    int fctr = qRound(1.0/(p.bug.aiDownsampleFactor > 0. ? p.bug.aiDownsampleFactor : 1.0));
    for (int i = 0; i < extraAI->rate->count(); ++i) {
        if (qRound(qPow(2.0,double(i))) == fctr) extraAI->rate->setCurrentIndex(i);
    }

    for (int i = 0; i < dialog->ttlTrigCB->count(); ++i) {
        if (p.bug.aiTrig.compare(dialog->ttlTrigCB->itemText(i))==0)
                selected_ai_cb_ix = i;
    }
    if (p.bug.ttlTrig > -1)
        dialog->ttlTrigCB->setCurrentIndex(p.bug.ttlTrig+3);
    else if (p.bug.auxTrig > -1)
        dialog->ttlTrigCB->setCurrentIndex(p.bug.auxTrig+1);
    else if (selected_ai_cb_ix > -1)
        dialog->ttlTrigCB->setCurrentIndex(selected_ai_cb_ix);
    else
        dialog->ttlTrigCB->setCurrentIndex(0);
    dialog->clkEdgeCB->setCurrentIndex(p.bug.clockEdge);
	if (p.bug.hpf > 0) {
		dialog->hpfSB->setValue(p.bug.hpf);
		dialog->hpfChk->setChecked(true);
	} else {
		dialog->hpfChk->setChecked(false);
	}
	dialog->notchFilterChk->setChecked(p.bug.snf);
	dialog->errTolSB->setValue(p.bug.errTol);
	for (int i = 1; i < dialog->graphsPerTabCB->count(); ++i) {
        if (dialog->graphsPerTabCB->itemText(i).toUInt() == (unsigned)p.overrideGraphsPerTab) {
            dialog->graphsPerTabCB->setCurrentIndex(i);
            break;
        }
    }	
	
	dialog->trigWSB->setValue(p.pdThreshW);
	dialog->trigStopTimeSB->setValue(p.pdStopTime);
	dialog->trigPre->setValue(p.silenceBeforePD*1000.);
	dialog->trigParams->setEnabled(p.bug.ttlTrig >= 0);
    dialog->trigThreshSB->setValue(p.bug.trigThreshV);

    mainApp()->configureDialogController()->probeDAQHardware();
    mainApp()->configureDialogController()->resetAOPassFromParams(aoPassThru, &p, &p.bug.aoSrate);
    aoPassThru->aoPassthruGB->setCheckable(true);
    aoPassThru->aoPassthruGB->setChecked(p.aoPassthru && aoPassThru->aoDeviceCB->count());
    if (!aoPassThru->aoDeviceCB->count()) aoPassThru->aoPassthruGB->setEnabled(false);
    // alt ttl stuff
    dialog->ttlAltChk->setChecked(p.bug.altTTL);
    dialog->trigPre_2->setValue(p.silenceBeforePD*1000.0);
    dialog->trigPost->setValue(p.pdStopTime*1000.0);
    dialog->trigWSB_2->setValue(p.pdThreshW);
    dialog->trigAltThreshSB->setValue(p.bug.trigThreshV);
    ttlAltClicked(p.bug.altTTL);

    //polish
    aoDeviceCBChanged();
    ttlTrigCBChanged();

    //set range from saved gui
    for (int i = 0; i < extraAI->rngCB->count(); ++i) {
        if (p.bug.guiAIRange.compare(extraAI->rngCB->itemText(i)) == 0) {
            extraAI->rngCB->setCurrentIndex(i);
            break;
        }
    }
}

void Bug_ConfigDialog::saveSettings()
{
	mainApp()->configureDialogController()->saveSettings();
}

Bug_ConfigDialog::ValidationResult Bug_ConfigDialog::validateForm(QString & errTitle, QString & errMsg, bool isGUI)
{
	errTitle = ""; errMsg = ""; (void)isGUI;
/*	if (dialog->ttlTrigCB->currentIndex() && dialog->ttlTrigCB->currentIndex() == dialog->ttl2CB->currentIndex()) {
		errTitle = "Duplicate TTL Channel";
		errMsg = QString("The same TTL channel (%1) was specified twice in both TTL combo boxes. Try again.").arg(dialog->ttl2CB->currentIndex()-1);
		return AGAIN;					
	}*/
    if ( (!dialog->ttlAltChk->isChecked() && (dialog->trigStopTimeSB->value() >= 10.0 || dialog->trigPre->value() >= 10000))
         || (dialog->ttlAltChk->isChecked() && (dialog->trigPost->value() >= 10000 || dialog->trigPre_2->value() >= 10000)) ) {
        int res = QMessageBox::warning(0,"Large Pre/Post Window Specified", "The trigger pre/post window size is a bit large (>10s). This may use up a lot of memory and lead to performance issues. Proceed anyway?",QMessageBox::Ok|QMessageBox::Retry, QMessageBox::Retry);
        if (res == QMessageBox::Retry) return AGAIN;
    }
	QVector<unsigned> subsetChans;
    QString subsetString = dialog->channelSubsetLE->text().trimmed();	
	if (!subsetString.size()) subsetString = "ALL";
	const bool hasAllSubset =  !subsetString.compare("ALL", Qt::CaseInsensitive) || subsetString == "*";
	bool err = false;
	subsetString = ConfigureDialogController::parseAIChanString(subsetString, subsetChans, &err, true);
	if (err && !hasAllSubset) {
		errTitle = "Channel subset error.";
		errMsg = "The channel subset is incorrectly defined.";
		return AGAIN;
	} else if (hasAllSubset) {
		dialog->channelSubsetLE->setText("ALL");
	}
	return OK;
}

void Bug_ConfigDialog::ttlTrigCBChanged()
{
	for (int i = 0; i < DAQ::BugTask::TotalTTLChans; ++i)
		ttls[i]->setEnabled(true);
    const int idx = dialog->ttlTrigCB->currentIndex(), limit_non_ai_idx = DAQ::BugTask::TotalAuxChans+DAQ::BugTask::TotalTTLChans+1;
    if (idx > 2 && dialog->ttlTrigCB->currentIndex() < limit_non_ai_idx) {
        ttls[dialog->ttlTrigCB->currentIndex()-3]->setEnabled(false);
	}
    const bool isLineSelected = idx != 0;
    dialog->trigParams->setEnabled(isLineSelected);
    dialog->altTrigParams->setEnabled(isLineSelected);
    dialog->ttlAltChk->setEnabled(isLineSelected);
    dialog->graphsPerTabCB->setEnabled(!isLineSelected);
    dialog->graphsPerTabLabel->setEnabled(!isLineSelected);
    if (isLineSelected) {
        dialog->graphsPerTabCB->setCurrentIndex(0);
    }
    if (idx >= limit_non_ai_idx) { // ai channel selected.. force first extraAI channel to reflect this!
        QString txt = dialog->ttlTrigCB->currentText();
        extraAI->chan1->setEnabled(false);
        for (int i = 0; i < extraAI->chan1->count(); ++i) {
            if (extraAI->chan1->itemText(i).compare(txt) == 0) {
                extraAI->chan1->setCurrentIndex(i); break;
            }
        }
    } else {
        extraAI->chan1->setEnabled(true);
    }
    extraAI->trigBackupChk->setDisabled(extraAI->chan1->currentIndex()==0 || dialog->ttlTrigCB->currentIndex()==0 || !extraAI->chan1->isEnabled());
    bool threshEn = extraAI->trigBackupChk->isEnabled() && extraAI->trigBackupChk->isChecked();

    if (!extraAI->trigBackupChk->isEnabled()) {
        extraAI->trigBackupChk->setChecked(false);
    }
    extraAI->thresh->setEnabled(threshEn);
    extraAI->threshLbl->setEnabled(threshEn);

    // setup the range setting combo box appropriately
    if (extraAI->chan1->currentIndex()==0 && extraAI->chan2->currentIndex() == 0) {
        extraAI->rngCB->setDisabled(true);
        extraAI->rngLbl->setDisabled(true);
    } else {
        QString dev = extraAI->chan1->currentIndex()!=0 ? extraAI->chan1->currentText() : extraAI->chan2->currentText();
        dev = dev.contains("/") ? dev.split("/").front() : dev;
        DAQ::DeviceRangeMap rm = DAQ::ProbeAllAIRanges();
        if (rm.contains(dev)) {
            QString oldR = extraAI->rngCB->currentText();
            extraAI->rngCB->clear();
            int i = 0;
            for (DAQ::DeviceRangeMap::const_iterator it = rm.find(dev); it != rm.end() && dev.compare(it.key()) == 0; ++it, ++i) {
                const DAQ::Range & r = it.value();
                QString item = QString("%1-%2 V").arg(r.min).arg(r.max);
                extraAI->rngCB->addItem(item);
                if (item.compare(oldR)==0) extraAI->rngCB->setCurrentIndex(i);
            }
        }
        if (extraAI->rngCB->count()) {
            extraAI->rngCB->setEnabled(true); extraAI->rngLbl->setEnabled(true);
        }
    }
}

void Bug_ConfigDialog::aoBufferSizeSliderChanged()
{
    ConfigureDialogController::updateAOBufferSizeLabel(aoPassThru);
}

void Bug_ConfigDialog::aoDeviceCBChanged()
{
    mainApp()->configureDialogController()->updateAORangeOnCBChange(aoPassThru);
}

void Bug_ConfigDialog::ttlAltClicked(bool b)
{
    dialog->altTrigParams->setEnabled(b);
    dialog->altTrigParams->setHidden(!b);
    dialog->trigParams->setEnabled(!b);
    dialog->trigParams->setHidden(b);
}
