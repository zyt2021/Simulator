#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mDockWidget(nullptr)
    , threadWait(false)
{
    ui->setupUi(this);

#if _DEBUG
    setWindowTitle("Simulator_d");
#else
    setWindowTitle("Simulator");
#endif    

    //init occ
    aMdlWidget = new OCCWidget(this);
    QGridLayout *centerLayOut = new QGridLayout;
    centerLayOut->addWidget(aMdlWidget);
    ui->centralwidget->setLayout(centerLayOut);

    statusLabel = new QLabel(this);
    statusLabel->setMinimumWidth(200);
    ui->statusbar->addPermanentWidget(statusLabel);

    //add the progress on the task bar
    mTaskBarButton = new QWinTaskbarButton(this);
    mTaskBarButton->setWindow(this->windowHandle());

    mTaskBarProgress = mTaskBarButton->progress();
    mTaskBarProgress->setVisible(true);
    mTaskBarProgress->setValue(10);

    creatDockWidgetToolBar();
    creatEditLocationDock();

    QRibbon::install(this);    

    mProcessData = new ProcessDataWidget;
    Handle(AIS_Shape) processModel = new AIS_Shape(mProcessData->getShape());
    aMdlWidget->getContext()->Display(processModel,Standard_False);

    mTaskBarProgress->setValue(30);

    ui->statusbar->showMessage(tr("Loading Data, Please Wait........."));
}

MainWindow::~MainWindow()
{
    delete mPlannerThread;
    delete mProcessData;
    delete ui;
}

void MainWindow::initRL()
{
    QString JointMdlFile = "./mdl/GP8.xml";
    QString JoingtSgFile = "./scene/GP_DMISModel.xml";
    QString JointModelFile = "./brep/GP8";
    aConvertAPI = new RLConvertAPI(JointMdlFile,JoingtSgFile,JointModelFile,this);
    aConvertAPI->InitLoadData();

    mTaskBarProgress->setValue(80);

    connect(aConvertAPI,&RLConvertAPI::JointPositionChanged,this,[=](){
        aMdlWidget->getView()->Update();
        if(RLAPI_PlanThread::PlannerSolved)
        {
            displayJointPosition();
            displayOperationalPosition();
        }
    });
    connect(aConvertAPI,&RLConvertAPI::JointCollision,this,[=](const size_t &index){
        statusLabel->setText(tr("Joint %1 COLLISION!!!").arg(index));
        aMdlWidget->getView()->SetBgGradientColors(Quantity_NOC_RED4,Quantity_NOC_WHITESMOKE,Aspect_GFM_VER);
    });
    connect(aConvertAPI,&RLConvertAPI::SelfCollision,this,[=](const size_t &aIndex, const size_t &bIndex){
        statusLabel->setText(tr("Joint %1 and Joint %2 COLLISION!!!").arg(aIndex).arg(bIndex));
        aMdlWidget->getView()->SetBgGradientColors(Quantity_NOC_RED4,Quantity_NOC_WHITESMOKE,Aspect_GFM_VER);
    });
    connect(aConvertAPI,&RLConvertAPI::NoCollision,this,[=](){
        statusLabel->setText(tr("NOT COLLISION"));
        aMdlWidget->getView()->SetBgGradientColors(Quantity_NOC_BLUE4,Quantity_NOC_WHITESMOKE,Aspect_GFM_VER);
    });

    for (int i=0;i<aConvertAPI->GetJointModelShapes().size();++i)
    {
        Handle(AIS_Shape) aShape = aConvertAPI->GetJointModelShapes().at(i);
        if(i==0||i==1)
            aShape->SetColor(Quantity_Color(0.4,0.4,0.4,Quantity_TOC_RGB));
        aMdlWidget->getContext()->Display(aShape,Standard_False);
        aMdlWidget->getContext()->Deactivate(aShape);
    }

    mTaskBarProgress->setValue(90);

    aMdlWidget->getView()->FitAll();

    creatConfigDock();
    creatOperationDock();

    mPlannerThread = new RLAPI_PlanThread(*aConvertAPI->GetMdlDynamic(),*aConvertAPI->GetSolidScene(),aConvertAPI->GetModelMinSize());
    qRegisterMetaType<rl::math::Vector>("rl::math::Vector");
    qRegisterMetaType<ComputeError>("ComputeError");
    connectThread();

//    mStartVec = aConvertAPI->GetJointPosition();
//    parseProcessData();

    mTaskBarProgress->setValue(100);
    mTaskBarProgress->setVisible(false);
    ui->statusbar->showMessage(tr("Init Success!"));
}

void MainWindow::creatConfigDock()
{
    QTableView *configTable = new QTableView;
    configTable->setWindowTitle(tr("Configuration"));
    configTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    configTable->horizontalHeader()->hide();

    tableViewJointDelegate *configDelegate = new tableViewJointDelegate(this);
    configDelegate->setMaxValue(aConvertAPI->MotionMaxValues);
    configDelegate->setMinValue(aConvertAPI->MotionMinValues);
    configDelegate->setJointType(aConvertAPI->GetJointType());

    mConfigModel = new tableViewJointModel(this);
    mConfigModel->SetMaxValue(aConvertAPI->MotionMaxValues);
    mConfigModel->SetMinValue(aConvertAPI->MotionMinValues);
    mConfigModel->SetJointType(aConvertAPI->GetJointType());
    mConfigModel->SetJointModelDofs(aConvertAPI->GetJointModelDof());
    mConfigModel->initData(aConvertAPI->GetJointPosition());

    configTable->setItemDelegate(configDelegate);
    configTable->setModel(mConfigModel);

    connect(mConfigModel,&tableViewJointModel::changePositionAndValue,this,[=](const int& index,const double& value){
        aConvertAPI->SetIndexedJointValue(index,value);
        displayOperationalPosition();
    });

    mConfigDock = new CustomDockWidget;
    connect(mConfigDock, &CustomDockWidget::signal_pinned, this, &MainWindow::dockWidgetPinned);
    connect(mConfigDock, &CustomDockWidget::signal_unpinned, this, &MainWindow::dockWidgetUnpinned);
    connect(mConfigDock, &CustomDockWidget::signal_docked, this, &MainWindow::dockWidgetDocked);
    connect(mConfigDock, &CustomDockWidget::signal_undocked, this, &MainWindow::dockWidgetUndocked);
    mConfigDock->setWidget(configTable);
    addDockWidget(Qt::LeftDockWidgetArea,mConfigDock);
}

void MainWindow::creatOperationDock()
{
    QTableView *operationTable = new QTableView;
    operationTable->setWindowTitle(tr("Operation"));
    operationTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    operationTable->verticalHeader()->hide();    

    tableViewPosDelegate *operationDelegate = new tableViewPosDelegate(this);

    mOperationModel = new tableViewPosModel(this);
    mOperationModel->initData(aConvertAPI->GetOperationalPosition());

    operationTable->setItemDelegate(operationDelegate);
    operationTable->setModel(mOperationModel);

    connect(mOperationModel,&tableViewPosModel::changePositionAndValue,this,[=](const int& index,const double& value){
        bool solve = aConvertAPI->SetIndexedInverseValue(index,value);
        if(!solve)
            mOperationModel->initData(aConvertAPI->GetOperationalPosition());
        displayJointPosition();
    });

    mOperationDock = new CustomDockWidget;
    connect(mOperationDock, &CustomDockWidget::signal_pinned, this, &MainWindow::dockWidgetPinned);
    connect(mOperationDock, &CustomDockWidget::signal_unpinned, this, &MainWindow::dockWidgetUnpinned);
    connect(mOperationDock, &CustomDockWidget::signal_docked, this, &MainWindow::dockWidgetDocked);
    connect(mOperationDock, &CustomDockWidget::signal_undocked, this, &MainWindow::dockWidgetUndocked);
    mOperationDock->setWidget(operationTable);
    addDockWidget(Qt::BottomDockWidgetArea,mOperationDock);
    this->resizeDocks({mOperationDock},{100},Qt::Vertical);
}

void MainWindow::displayJointPosition()
{
    mConfigModel->initData(aConvertAPI->GetJointPosition());
    mConfigModel->updateModel();
}

void MainWindow::displayOperationalPosition()
{
    mOperationModel->initData(aConvertAPI->GetOperationalPosition());
    mOperationModel->updateModel();
}

void MainWindow::drawPathLine()
{
    QList<double> currentPos = aConvertAPI->GetOperationalPosition();
    gp_Pnt tmp(currentPos.value(0),currentPos.value(1),currentPos.value(2));

    if((currentPahtPnt.X()==0)&&(currentPahtPnt.Y()==0)&&(currentPahtPnt.Z()==0))
        currentPahtPnt = tmp;
    if((currentPahtPnt.X()==tmp.X())&&(currentPahtPnt.Y()==tmp.Y())&&(currentPahtPnt.Z()==tmp.Z()))
        return;

    Handle(Geom_TrimmedCurve) geom_line =  GC_MakeSegment(currentPahtPnt,tmp).Value();
    TopoDS_Shape t_topo_line = BRepBuilderAPI_MakeEdge(geom_line);
    Handle(AIS_Shape) anAIS = new AIS_Shape(t_topo_line);
    anAIS->SetColor(Quantity_NOC_SEAGREEN1);
    anAIS->SetWidth(3);
    pathLines.append(anAIS);
    aMdlWidget->getContext()->Display(anAIS,false);

    currentPahtPnt = tmp;
}

void MainWindow::connectThread()
{
    connect(mPlannerThread,&RLAPI_PlanThread::ComputeSuccess,this,[=](int time){
        statusLabel->setText(tr("Solved %1ms,Optimizing...").arg(time));
    });
    connect(mPlannerThread,&RLAPI_PlanThread::ReadyToSetJointValue,this,[=](const rl::math::Vector &thePos){
        aConvertAPI->SetJointValue(thePos);
        drawPathLine();
    });
    connect(mPlannerThread,&RLAPI_PlanThread::ComputeOver,this,[=](double pathLength){
        totalPathLen += pathLength;
        statusLabel->setText(tr("Solved, Value %1").arg(totalPathLen));
        if(endsIterator!=optimizedEndList.end())
        {
            mPlannerThread->GetComputeArguments(lastEndVect,*endsIterator);
            lastEndVect = *endsIterator;
            endsIterator++;
            mPlannerThread->start();
        }
        else
        {
            mPlannerThread->quit();
        }
        currentPahtPnt = gp_Pnt(0,0,0);
    });
    connect(mPlannerThread,&RLAPI_PlanThread::ComputeTimeOut,this,[=](){
//        mPlannerThread->terminate();//joints in mess ,need to init position after!!!
//        aConvertAPI->SetJointValue(mStartVec);
//        statusLabel->setText(tr("Compute Time Out!"));
    });
    connect(mPlannerThread,&RLAPI_PlanThread::ComputeFailed,this,[=](const ComputeError &errorCode){
        if(errorCode==ComputeError::InvalidConfig)
            QMessageBox::critical(this,tr("error"),tr("Invalid start point or end point!"));
        else if(errorCode==ComputeError::ArgumentError)
            QMessageBox::critical(this,tr("error"),tr("Unsuitable planner arguments"));

        mPlannerThread->quit();
        statusLabel->setText(tr("Compute Failed!"));
    });
}

void MainWindow::creatDockWidgetToolBar()
{
    Qt::DockWidgetArea area = Qt::LeftDockWidgetArea;
    CustomDockTabBar* leftDockWidgetBar = new CustomDockTabBar(area);
    mDockWidgetToolBar[area] = leftDockWidgetBar;
    connect(leftDockWidgetBar, &CustomDockTabBar::signal_dockWidgetButton_clicked, this, &MainWindow::showDockWidget);
    addToolBar(dockAreaToToolBarArea(area), leftDockWidgetBar);
    area = Qt::RightDockWidgetArea;
    CustomDockTabBar* rightDockWidgetBar = new CustomDockTabBar(area);
    mDockWidgetToolBar[area] = rightDockWidgetBar;
    connect(rightDockWidgetBar, &CustomDockTabBar::signal_dockWidgetButton_clicked, this, &MainWindow::showDockWidget);
    addToolBar(dockAreaToToolBarArea(area), rightDockWidgetBar);
    area = Qt::TopDockWidgetArea;
    CustomDockTabBar* topDockWidgetBar = new CustomDockTabBar(area);
    mDockWidgetToolBar[area] = topDockWidgetBar;
    connect(topDockWidgetBar, &CustomDockTabBar::signal_dockWidgetButton_clicked, this, &MainWindow::showDockWidget);
    addToolBar(dockAreaToToolBarArea(area), topDockWidgetBar);
    area = Qt::BottomDockWidgetArea;
    CustomDockTabBar* bottomDockWidgetBar = new CustomDockTabBar(area);
    mDockWidgetToolBar[area] = bottomDockWidgetBar;
    connect(bottomDockWidgetBar, &CustomDockTabBar::signal_dockWidgetButton_clicked, this, &MainWindow::showDockWidget);
    addToolBar(dockAreaToToolBarArea(area), bottomDockWidgetBar);
}

void MainWindow::hideDockWidget(CustomDockWidget *dockWidget)
{
    if((dockWidget == nullptr) || (dockWidget->isHidden())) {
        return;
    }

    mDockWidget = nullptr;

    dockWidget->hide();
}

void MainWindow::dockWidgetPinned(CustomDockWidget *dockWidget)
{
    if(dockWidget == nullptr) {
        return;
    }

    CustomDockTabBar* dockWidgetBar = getDockWidgetBar(dockWidget->getArea());
    if(dockWidgetBar == nullptr) {
        return;
    }

    mDockWidget = nullptr;

    std::vector<CustomDockWidget*> dockWidgetList = dockWidget->getTabifiedDocks();
    dockWidgetList.push_back(dockWidget);

    CustomDockWidget* prevDockWidget = nullptr;

    std::for_each(std::begin(dockWidgetList), std::end(dockWidgetList), [&](CustomDockWidget* dockWidget)
    {
        if(dockWidgetBar->removeDockWidget(dockWidget))
        {
            if(prevDockWidget == nullptr) {
                QMainWindow::addDockWidget(dockWidget->getArea(), dockWidget);
            }
            else {
                tabifyDockWidget(prevDockWidget, dockWidget);
            }

            prevDockWidget = dockWidget;

            dockWidget->setDockWidgetState(DockWidgetState::Docked);

            dockWidget->show();
        }
    } );

    dockWidget->raise();
}

void MainWindow::dockWidgetUnpinned(CustomDockWidget *dockWidget)
{
    if(dockWidget == nullptr) {
        return;
    }

    CustomDockTabBar* dockWidgetBar = getDockWidgetBar(dockWidget->getArea());
    if(dockWidgetBar == nullptr) {
        return;
    }

    QList<QDockWidget*> dockWidgetList = tabifiedDockWidgets(dockWidget);
    dockWidgetList.push_back(dockWidget);

    std::for_each(std::begin(dockWidgetList), std::end(dockWidgetList), [&](QDockWidget* qDockWidget)
    {
        CustomDockWidget* dockWidget = static_cast<CustomDockWidget*>(qDockWidget);

        dockWidget->setDockWidgetState(DockWidgetState::Hidden);

        if(!dockWidget->isHidden())
        {
            dockWidgetBar->addDockWidget(dockWidget);

            dockWidget->setTabifiedDocks(dockWidgetList);

            QMainWindow::removeDockWidget(dockWidget);
        }
    } );
}

void MainWindow::dockWidgetDocked(CustomDockWidget *dockWidget)
{
    if(dockWidget == nullptr) {
        return;
    }
}

void MainWindow::dockWidgetUndocked(CustomDockWidget *dockWidget)
{
    hideDockWidget(mDockWidget);

    CustomDockTabBar* dockWidgetBar = getDockWidgetBar(dockWidget->getArea());
    if(dockWidgetBar == nullptr) {
        return;
    }

    dockWidget->clearTabifiedDocks();

    if(dockWidgetBar->removeDockWidget(dockWidget))
    {
        if(!dockWidget->isFloating()) {
            QMainWindow::addDockWidget(dockWidget->getArea(), dockWidget);
        }

        dockWidget->show();
    }
}

Qt::ToolBarArea MainWindow::dockAreaToToolBarArea(Qt::DockWidgetArea area)
{
    switch(area)
    {
    case Qt::LeftDockWidgetArea: return Qt::LeftToolBarArea;
    case Qt::RightDockWidgetArea: return Qt::RightToolBarArea;
    case Qt::TopDockWidgetArea: return Qt::TopToolBarArea;
    case Qt::BottomDockWidgetArea: return Qt::BottomToolBarArea;
    default:
        return Qt::ToolBarArea(0);
    }
}

CustomDockTabBar *MainWindow::getDockWidgetBar(Qt::DockWidgetArea area)
{
    Q_ASSERT(mDockWidgetToolBar.find(area) != std::end(mDockWidgetToolBar));

    auto it = mDockWidgetToolBar.find(area);
    if(it != std::end(mDockWidgetToolBar)) {
        return it->second;
    }

    return nullptr;
}

void MainWindow::showDockWidget(CustomDockWidget *dockWidget)
{
    if(dockWidget == nullptr) {
        return;
    }

    if(dockWidget->isHidden())
    {
        hideDockWidget(mDockWidget);

        if(dockWidget->isFloating())
        {
            QMainWindow::addDockWidget(dockWidget->getArea(), dockWidget);
            dockWidget->setFloating(false);

            QMainWindow::removeDockWidget(dockWidget);
        }

        dockWidget->show();
        dockWidget->raise();

        dockWidget->setFocus();

        mDockWidget = dockWidget;
    }
    else
    {
        hideDockWidget(dockWidget);
    }
}

void MainWindow::creatEditLocationDock()
{
    mEditDockDlg = new CustomDockWidget;
    connect(mEditDockDlg, &CustomDockWidget::signal_pinned, this, &MainWindow::dockWidgetPinned);
    connect(mEditDockDlg, &CustomDockWidget::signal_unpinned, this, &MainWindow::dockWidgetUnpinned);
    connect(mEditDockDlg, &CustomDockWidget::signal_docked, this, &MainWindow::dockWidgetDocked);
    connect(mEditDockDlg, &CustomDockWidget::signal_undocked, this, &MainWindow::dockWidgetUndocked);
    addDockWidget(Qt::RightDockWidgetArea,mEditDockDlg);
    hideDockWidget(mEditDockDlg);
}

void MainWindow::on_actionView_Start_Position_triggered()
{
    aConvertAPI->SetJointValue(mStartVec);
    displayJointPosition();
    displayOperationalPosition();
}

void MainWindow::on_actionView_End_Position_triggered()
{
    aConvertAPI->SetJointValue(*mEndList.begin());
    displayJointPosition();
    displayOperationalPosition();
}

void MainWindow::on_actionSet_Start_Position_triggered()
{
    mStartVec = aConvertAPI->GetJointPosition();
    statusLabel->setText(tr("start position set"));

    QList<double> currentPos = aConvertAPI->GetOperationalPosition();
    currentPahtPnt = gp_Pnt(currentPos.value(0),
                            currentPos.value(1),
                            currentPos.value(2)-1);//don't set them as same ,or can't draw the first part of the path line
}

void MainWindow::on_actionSet_End_Position_triggered()
{
    rl::math::Vector EndPos = aConvertAPI->GetJointPosition();
    mEndList.push_back(EndPos);
    statusLabel->setText(tr("end position set"));
}

void MainWindow::on_actionStart_Planner_triggered()
{
    statusLabel->setText(tr("Solving......"));

    // 1.remove the old path line
    if(pathLines.size()>0){
        for(int i=0;i<pathLines.size();++i)
        {
            aMdlWidget->getContext()->Erase(pathLines[i],false);
        }
    }
    pathLines.clear();

    // 2.optimize the path node
    if(mEndList.size()>1)
    {
        RLAPI_ConfigurationOptimizer anOptimizer;
        anOptimizer.SetStartConfigurations(mStartVec);
        anOptimizer.SetEndConfigurations(mEndList);
        anOptimizer.theDynamic = aConvertAPI->GetMdlDynamic();
        optimizedEndList = anOptimizer.Process();
    }
    else optimizedEndList = mEndList;

    // 3.start
    totalPathLen=0;
    endsIterator=optimizedEndList.begin();
    mPlannerThread->GetComputeArguments(mStartVec,*endsIterator);
    lastEndVect = *endsIterator;
    endsIterator++;
    mPlannerThread->start();
}

void MainWindow::on_actionPause_Planner_triggered()
{
    if(mPlannerThread->isRunning()&&!threadWait)
    {
        threadWait = true;
        ui->actionPause_Planner->setIcon(QIcon(":/Simulator/icons/thread_resume.png"));
        ui->actionPause_Planner->setToolTip(tr("Resume Thread"));
        mPlannerThread->pause();
    }
    else if(mPlannerThread->isRunning()&&threadWait)
    {
        threadWait = false;
        ui->actionPause_Planner->setIcon(QIcon(":/Simulator/icons/thread_pause.png"));
        ui->actionPause_Planner->setToolTip(tr("Pause Thread"));
        mPlannerThread->resume();
    }
}

void MainWindow::on_actionExit_Planner_triggered()
{
    if(!mPlannerThread->isRunning())
        return;

    int result = QMessageBox::warning(this,tr("warning"),tr("Sure to exit the plan thread?\n"
                                               "This may cause uncertain consequences."),QMessageBox::Yes,QMessageBox::No);
    if(result==QMessageBox::Yes)
        mPlannerThread->terminate();
    else
        return;
}

void MainWindow::on_actionImport_Model_triggered()
{
    QString modelFileName = QFileDialog::getOpenFileName(this,tr("choose file"),"",tr("Support Type(*.stp *.step *.STP *.STEP *.iges *.igs *.IGES *.IGS *.brep *.brp\n)"
                                                                                      "*.stp *.step *.STP *.STEP\n"
                                                                                      "*.iges *.igs *.IGES *.IGS\n"
                                                                                      "*.brep *.brp"));
    if(modelFileName.isEmpty())
        return;

    mTaskBarProgress->setVisible(true);
    mTaskBarProgress->setValue(10);

    aConvertAPI->ImportSceneModel(modelFileName);    
    mTaskBarProgress->setValue(30);

    aMdlWidget->getContext()->Erase(aConvertAPI->GetMeasureModelShape(),Standard_False);
    mTaskBarProgress->setValue(40);

    aConvertAPI->ResetSceneModel();
    mTaskBarProgress->setValue(80);

    mPlannerThread->deleteLater();
    mPlannerThread = new RLAPI_PlanThread(*aConvertAPI->GetMdlDynamic(),*aConvertAPI->GetSolidScene(),aConvertAPI->GetModelMinSize());
    connectThread();

    mConfigModel->initData(aConvertAPI->GetJointPosition());
    mOperationModel->initData(aConvertAPI->GetOperationalPosition());

    aMdlWidget->getContext()->Display(aConvertAPI->GetMeasureModelShape(),Standard_False);
    aMdlWidget->getView()->FitAll();

    mTaskBarProgress->setValue(10);
    mTaskBarProgress->setVisible(false);
}

void MainWindow::on_actionOperate_Model_triggered()
{
    if(!aMdlWidget->getManipulator()->IsAttached())
    {
        QInputDialog aDialog;
        bool ok;
        int index = aDialog.getInt(this,tr("oprate"),tr("Joint"),0,0,aConvertAPI->GetJointModelDof(),1,&ok);
        if(ok)
            aMdlWidget->getManipulator()->Attach(aConvertAPI->GetJointModelShapes().at(index));
        else return;
    }
    else
        aMdlWidget->getManipulator()->Detach();

    aMdlWidget->getView()->Update();
}

void MainWindow::on_actionSave_As_Picture_triggered()
{
    QString picName = QFileDialog::getSaveFileName(this,tr("save file"),"","*.bmp *.png *.jpg");
    if(picName.isEmpty())
        return;

    Image_PixMap map;
    aMdlWidget->getView()->ToPixMap(map,aMdlWidget->width(),aMdlWidget->height(),Graphic3d_BT_RGBA);
    QImage image = QImage(map.Data(),aMdlWidget->width(),aMdlWidget->height(),QImage::Format_RGBA8888);
    image = image.mirrored(false, true);//need to mirror
    image.save(picName);
}

void MainWindow::on_actionView_Back_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Ypos);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Top_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Zpos);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Front_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Yneg);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Bottom_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Zneg);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Left_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Xneg);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Right_triggered()
{
    aMdlWidget->getView()->SetProj(V3d_Xpos);
    aMdlWidget->getView()->FitAll();
}

void MainWindow::on_actionView_Shade_triggered()
{
    aMdlWidget->getContext()->SetDisplayMode(AIS_Shaded,Standard_True);
    ui->menuDisplay_Model->setIcon(QIcon(":/Simulator/icons/view_shade.png"));
}

void MainWindow::on_actionView_Wire_triggered()
{
    aMdlWidget->getContext()->SetDisplayMode(AIS_WireFrame,Standard_True);
    ui->menuDisplay_Model->setIcon(QIcon(":/Simulator/icons/view_wire.png"));
}

void MainWindow::parseProcessData()
{
    QList<TcpData> tcpPnts = mProcessData->getProcessPnts();
    aConvertAPI->LockBasePosition();
    QList<double> tcpInfo;

    for(int k=0;k<tcpPnts.size();++k)
    {
        tcpInfo.clear();

        tcpInfo.append(tcpPnts[k].tcpPos.X());
        tcpInfo.append(tcpPnts[k].tcpPos.Y());
        tcpInfo.append(tcpPnts[k].tcpPos.Z());
        tcpInfo.append(tcpPnts[k].VX);
        tcpInfo.append(tcpPnts[k].VY);
        tcpInfo.append(tcpPnts[k].VZ);qDebug()<<tcpPnts[k].tcpPos.X()<<tcpPnts[k].tcpPos.Y()<<tcpPnts[k].tcpPos.Z()<<tcpPnts[k].VX<<tcpPnts[1].VY<<tcpPnts[k].VZ;

        if(aConvertAPI->SetInverseValue(tcpInfo))
            mEndList.push_back(aConvertAPI->GetJointPosition());
    }
}

void MainWindow::on_actionEdit_Location_triggered()
{
    if(EditLocationWidget::existOne)
    {
        QMessageBox::critical(this,tr("error"),tr("there is an edit widget!"));
        return;
    }

    EditLocationWidget *aWidget = new EditLocationWidget;
    QStringList aList;
    for(std::size_t i=0;i<aConvertAPI->GetJointModelDof()+1;++i)
        aList<<QString("Link%1").arg(i);

    aWidget->setEditJointNameList(aList);
    aWidget->show();

    connect(aWidget,&EditLocationWidget::applyTrsf,this,[=](const int &index, const gp_Trsf &aTrsf){
        gp_Trsf before = aConvertAPI->GetJointModelShapes().at(index)->LocalTransformation();
        aConvertAPI->GetJointModelShapes().at(index)->SetLocalTransformation(before.Multiplied(aTrsf));
        aMdlWidget->getView()->Update();
    });
    connect(aWidget,&EditLocationWidget::requestClose,this,[=](){
        mEditDockDlg->removeWidget();
        hideDockWidget(mEditDockDlg);
    });

    mEditDockDlg->setWidget(aWidget);
    if(mEditDockDlg->isHidden())
        showDockWidget(mEditDockDlg);
}

void MainWindow::on_actionDH_Setting_triggered()
{
    DHSettingWidget *aWidget = new DHSettingWidget();
    aWidget->show();
    connect(aWidget,&DHSettingWidget::destroyed,aWidget,&DHSettingWidget::deleteLater);

    RLAPI_DHSetting aDHSetting;
    aDHSetting.Compute();
    for(int i=0;i<aDHSetting.GetCoords().size();++i)
    {
        aMdlWidget->getContext()->Display(aDHSetting.GetCoords()[i],Standard_True);
    }
}

void MainWindow::on_actionProcess_Data_triggered()
{    
    mProcessData->show();    
}
