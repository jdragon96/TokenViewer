# Development Rule

- Qt5로 상용 배포 시 라이센스 비용 문제 없는 어플리케이션 개발
- C++

### 1. QThread Pattern

- Follow the next rules
- One thread, One files

#### 1.1. Header

```c++
// Header
#pragma once

#include <memory>
#include <QObject>

class MainThreadWorker: public QObject {
	Q_OBJECT

public:
	explicit MainThreadWorker(QObject* parent = nullptr);
	~MainThreadWorker();

	void SetContext(HOctWorkflowManager* pWorkMgr);

	void Stop();

	bool IsThreadAlive() const;

public slots:
	void DoWork();

signals:
	void WorkFinished(bool success);

	void ErrorOccurred(int code);

private:
	class WorkerImpl;
	std::unique_ptr<WorkerImpl> m_pimpl;
};

class MainThread : public QObject {
	Q_OBJECT

public:
	explicit MainThread(hios* kpHIOS);

	~MainThread();

	void start();

	void stop();

signals:
	void Finished(bool success);

	void errorDetected(int errorCode);

private:
	class MainThreadImpl;

	std::unique_ptr<MainThreadImpl> m_pimpl;
};
```

#### 1.2. Source

```c++
#include "pch.h"
#include "MainThread.h"
#include "HMemoryDebug.h"

////////////////////////////////////////////////////////////////////////////////
// WorkerImpl
////////////////////////////////////////////////////////////////////////////////
class MainThreadWorker::MainThreadWorkerImpl {
public:
	std::atomic<bool> doRunThread{ false };

	void DoWork();
};

void MainThreadWorker::MainThreadWorkerImpl::DoWork()
{
  // Work
}

////////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////////
MainThreadWorker::MainThreadWorker(QObject* parent)
	: QObject(parent)
	, m_pimpl(std::make_unique<MainThreadWorkerImpl>())
{
  // Worker 필요한 리소스 할당
}

HOCTTrackingWorker::~HOCTTrackingWorker()
{
  // 할당 해제
}

void MainThreadWorker::SetContext(HOctWorkflowManager* pWorkMgr)
{
	m_pimpl->pOctWorkManager = pWorkMgr;
}

void MainThreadWorker::Stop()
{
	m_pimpl->doRunThread = false;
}

bool MainThreadWorker::IsThreadAlive() const
{
	return m_pimpl->doRunThread;
}

void MainThreadWorker::DoWork()
{
	do {
        m_pimpl->m_pimpl();

		QCoreApplication::processEvents();
	} while (m_pimpl->doRunThread);

	emit WorkFinished(true);
}

////////////////////////////////////////////////////////////////////////////////
// ThreadImpl
////////////////////////////////////////////////////////////////////////////////
class MainThread::MainThreadImpl {
public:
	QThread workerThread;
	std::unique_ptr<MainThreadWorker, void(*)(QObject*)> worker;

	hios* kpHIOS;
	HOctWorkflowManager* kpOctWorkManager;

	MainThreadImpl(hios* pHIOS) : worker(nullptr, [](QObject* obj) { if (obj) obj->deleteLater(); })
    {
	}

	~MainThreadImpl() {
		stopWorker();
	}

	void connectWorker(HOCTTrackingThread* q_ptr) {
		if (!worker) return;
		QObject::connect(worker.get(), &MainThreadWorker::WorkFinished, q_ptr, &MainThread::Finished);
		QObject::connect(worker.get(), &MainThreadWorker::ErrorOccurred, q_ptr, &MainThread::errorDetected);
	}

	void stopWorker() {
		if (workerThread.isRunning()) {
			if (worker) worker->Stop();
			workerThread.quit();
			if (!workerThread.wait(4000)) {
				workerThread.terminate();
				workerThread.wait();
			}
		}
		worker.reset();
	}
};

////////////////////////////////////////////////////////////////////////////////
// Thread
////////////////////////////////////////////////////////////////////////////////
MainThread::MainThread(hios* kpHIOS)
	: m_pimpl(std::make_unique<MainThreadImpl>(kpHIOS)) {
}

MainThread::~MainThread() = default;

void MainThread::start()
{
	if (!m_pimpl->worker) {
		m_pimpl->worker.reset(new MainThreadWorker());
		m_pimpl->connectWorker(this);
		m_pimpl->kpOctWorkManager = m_pimpl->kpHIOS->m_kpOctWorkManager;
		m_pimpl->worker->SetContext(m_pimpl->kpOctWorkManager);
		m_pimpl->worker->moveToThread(&m_pimpl->workerThread);
	}

	if (!m_pimpl->workerThread.isRunning()) {
		m_pimpl->workerThread.start();
	}

	QMetaObject::invokeMethod(m_pimpl->worker.get(), "DoWork");
}

void MainThread::stop()
{
	m_pimpl->stopWorker();
}

#endif
```

## 2. Application Architecture

- 주요 모듈은 std::unique_ptr로 선언하고, Application 클래스에 정의해둔다.
- 모든 Widget은 아래 모듈을 injection 받고, 이를 사용하여 쇼하이드를 한다.

### 2.1. Communiation between UI

- Observers와 SystemStatus 클래스를 활용하여 UI간 통신을 수행한다.

#### 2.1.1 Observers

- Observer에 `Signal` 생성

```c++

#pragma once

#include <QObject>
#include <QString>

class Observers : public QObject
{
    Q_OBJECT

public:
    explicit Observers(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

signals:
    void messageChanged(const QString& message);
    void valueChanged(int value);
    void requestRefresh();
};

```

- Sender

```C++
class ControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ControlWidget(Observers& observers,
                           QWidget* parent = nullptr);

private:
    Observers& m_observers;
};

ControlWidget::ControlWidget(
    Observers& observers,
    QWidget* parent)
    : QWidget(parent)
    , m_observers(observers)
{
    connect(
        ui->spinBox,
        &QSpinBox::valueChanged,
        &m_observers,
        &Observers::valueChanged);
}
```

- Listener

```C++
class ViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ViewWidget(Observers& observers,
                        QWidget* parent = nullptr);

private slots:
    void onValueChanged(int value);

private:
    Observers& m_observers;
};

ViewWidget::ViewWidget(
    Observers& observers,
    QWidget* parent)
    : QWidget(parent)
    , m_observers(observers)
{
    connect(
        &m_observers,
        &Observers::valueChanged,
        this,
        &ViewWidget::onValueChanged);
}

void ViewWidget::onValueChanged(int value)
{
    ui->label->setText(QString::number(value));
}
```

### 2.1.2. SystemStatus

- Application이 가지고 있어얄 상태값을 보관하는 클래스이다.

### 2.2. UI Render Pattern

#### 2.2.1. UI 분기 로직

- 각 컴포넌트는 SystemStatus를 통해 내부 show, hide 함수에서 UI 분기 코드를 작성한다.

```C++

class SomeWidget: public QWidget
{
public:
    SomeWidget(Observers* kpObserver, SystemStatus* kpSystem);

public:
    virtual show() override
    {
        if (m_kpSyste->IsKnowledgetMode())
        {
            // TODO:
        }
        else
        {
            hide();
        }
    }

private:
    SystemStatus* m_kpSyste;
    Observers* m_kpObserver;
}

```

#### 2.2.2. UI 이벤트 처리

- 여러개의 컨트롤이 있을 때, 이벤트 처리 함수를 한 곳에서 처리한다.

```c++
ui->pushButtonA->installEventFilter(this);
ui->pushButtonB->installEventFilter(this);
ui->pushButtonC->installEventFilter(this);
```

```c++
bool MyWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
					if (watched == ui->pushButtonA)
					{
							// TODO
					}
					else if (watched == ui->pushButtonB)
					{
							// TODO
					}
    }

    return QWidget::eventFilter(watched, event);
}
```
