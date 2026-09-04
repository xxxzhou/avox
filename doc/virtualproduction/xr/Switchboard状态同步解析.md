> 整理自 aocec 仓库 `doc/virtualproduction/Switchboard.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# Switchboard状态同步解析

作为管理UE4使用多台机器nDisplay合作渲染工具,包含文件同步,多台机器状态监控等功能.

具主要代码在Engine\Source\Programs\SwitchboardListener模块里,此模块实现大部分功能,包含多台机器TCP连接,机器GPU/CPU信息获取,消息传递,文件同步更新等.

如下,在文件SwitchboardListener.cpp的方法FillOutSyncTopologies得到GPU同步等信息,以及相应发送与接收处理.

``` c++
USTRUCT()
struct FSyncTopo
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSyncGpu> SyncGpus;

	UPROPERTY()
	TArray<FSyncDisplay> SyncDisplays;

	UPROPERTY()
	FSyncStatusParams SyncStatusParams;

	UPROPERTY()
	FSyncControlParams SyncControlParams;
};
static void FillOutSyncTopologies(TArray<FSyncTopo>& SyncTopos)
{
	FScopeLock LockNvapi(&SwitchboardListenerMutexNvapi);

	// Normally there is a single sync card. BUT an RTX Server could have more, and we need to account for that.

	// Detect sync cards

	NvU32 GSyncCount = 0;
	NvGSyncDeviceHandle GSyncHandles[NVAPI_MAX_GSYNC_DEVICES];
	NvAPI_GSync_EnumSyncDevices(GSyncHandles, &GSyncCount); // GSyncCount will be zero if error, so no need to check error.

	for (NvU32 GSyncIdx = 0; GSyncIdx < GSyncCount; GSyncIdx++)
	{
		NvU32 GSyncGPUCount = 0;
		NvU32 GSyncDisplayCount = 0;

		// gather info first with null data pointers, just to get the count and subsequently allocate necessary memory.
		{
			const NvAPI_Status Result = NvAPI_GSync_GetTopology(GSyncHandles[GSyncIdx], &GSyncGPUCount, nullptr, &GSyncDisplayCount, nullptr);

			if (Result != NVAPI_OK)
			{
				NvAPI_ShortString ErrorString;
				NvAPI_GetErrorMessage(Result, ErrorString);
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetTopology failed. Error: %s"), ANSI_TO_TCHAR(ErrorString));
				continue;
			}
		}

		// allocate memory for data
		TArray<NV_GSYNC_GPU> GSyncGPUs;
		TArray<NV_GSYNC_DISPLAY> GSyncDisplays;
		{
			GSyncGPUs.SetNumUninitialized(GSyncGPUCount, false);

			for (NvU32 GSyncGPUIdx = 0; GSyncGPUIdx < GSyncGPUCount; GSyncGPUIdx++)
			{
				GSyncGPUs[GSyncGPUIdx].version = NV_GSYNC_GPU_VER;
			}

			GSyncDisplays.SetNumUninitialized(GSyncDisplayCount, false);

			for (NvU32 GSyncDisplayIdx = 0; GSyncDisplayIdx < GSyncDisplayCount; GSyncDisplayIdx++)
			{
				GSyncDisplays[GSyncDisplayIdx].version = NV_GSYNC_DISPLAY_VER;
			}
		}

		// get real info
		{
			const NvAPI_Status Result = NvAPI_GSync_GetTopology(GSyncHandles[GSyncIdx], &GSyncGPUCount, GSyncGPUs.GetData(), &GSyncDisplayCount, GSyncDisplays.GetData());

			if (Result != NVAPI_OK)
			{
				NvAPI_ShortString ErrorString;
				NvAPI_GetErrorMessage(Result, ErrorString);
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetTopology failed. Error: %s"), ANSI_TO_TCHAR(ErrorString));
				continue;
			}
		}

		// Build outbound structure

		FSyncTopo SyncTopo;

		for (NvU32 GpuIdx = 0; GpuIdx < GSyncGPUCount; GpuIdx++)
		{
			FSyncGpu SyncGpu;

			SyncGpu.bIsSynced = GSyncGPUs[GpuIdx].isSynced;
			SyncGpu.Connector = int32(GSyncGPUs[GpuIdx].connector);

			SyncTopo.SyncGpus.Emplace(SyncGpu);
		}

		for (NvU32 DisplayIdx = 0; DisplayIdx < GSyncDisplayCount; DisplayIdx++)
		{
			FSyncDisplay SyncDisplay;

			switch (GSyncDisplays[DisplayIdx].syncState)
			{
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_UNSYNCED:
				SyncDisplay.SyncState = TEXT("Unsynced");
				break;
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_SLAVE:
				SyncDisplay.SyncState = TEXT("Slave");
				break;
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_MASTER:
				SyncDisplay.SyncState = TEXT("Master");
				break;
			default:
				SyncDisplay.SyncState = TEXT("Unknown");
				break;
			}

			// get color information for each display
			{
				NV_COLOR_DATA ColorData;

				ColorData.version = NV_COLOR_DATA_VER;
				ColorData.cmd = NV_COLOR_CMD_GET;
				ColorData.size = sizeof(NV_COLOR_DATA);

				const NvAPI_Status Result = NvAPI_Disp_ColorControl(GSyncDisplays[DisplayIdx].displayId, &ColorData);

				if (Result == NVAPI_OK)
				{
					SyncDisplay.Bpc = ColorData.data.bpc;
					SyncDisplay.Depth = ColorData.data.depth;
					SyncDisplay.ColorFormat = ColorData.data.colorFormat;
				}
			}

			SyncTopo.SyncDisplays.Emplace(SyncDisplay);
		}

		// Sync Status Parameters
		{
			NV_GSYNC_STATUS_PARAMS GSyncStatusParams;
			GSyncStatusParams.version = NV_GSYNC_STATUS_PARAMS_VER;

			const NvAPI_Status Result = NvAPI_GSync_GetStatusParameters(GSyncHandles[GSyncIdx], &GSyncStatusParams);

			if (Result != NVAPI_OK)
			{
				NvAPI_ShortString ErrorString;
				NvAPI_GetErrorMessage(Result, ErrorString);
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetStatusParameters failed. Error: %s"), ANSI_TO_TCHAR(ErrorString));
				continue;
			}

			SyncTopo.SyncStatusParams.RefreshRate = GSyncStatusParams.refreshRate;
			SyncTopo.SyncStatusParams.HouseSyncIncoming = GSyncStatusParams.houseSyncIncoming;
			SyncTopo.SyncStatusParams.bHouseSync = !!GSyncStatusParams.bHouseSync;
			SyncTopo.SyncStatusParams.bInternalSlave = GSyncStatusParams.bInternalSlave;
		}

		// Sync Control Parameters
		{
			NV_GSYNC_CONTROL_PARAMS GSyncControlParams;
			GSyncControlParams.version = NV_GSYNC_CONTROL_PARAMS_VER;

			const NvAPI_Status Result = NvAPI_GSync_GetControlParameters(GSyncHandles[GSyncIdx], &GSyncControlParams);

			if (Result != NVAPI_OK)
			{
				NvAPI_ShortString ErrorString;
				NvAPI_GetErrorMessage(Result, ErrorString);
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetControlParameters failed. Error: %s"), ANSI_TO_TCHAR(ErrorString));
				continue;
			}

			SyncTopo.SyncControlParams.bInterlaced = !!GSyncControlParams.interlaceMode;
			SyncTopo.SyncControlParams.bSyncSourceIsOutput = !!GSyncControlParams.syncSourceIsOutput;
			SyncTopo.SyncControlParams.Interval = GSyncControlParams.interval;
			SyncTopo.SyncControlParams.Polarity = GSyncControlParams.polarity;
			SyncTopo.SyncControlParams.Source = GSyncControlParams.source;
			SyncTopo.SyncControlParams.VMode = GSyncControlParams.vmode;

			SyncTopo.SyncControlParams.SyncSkew.MaxLines = GSyncControlParams.syncSkew.maxLines;
			SyncTopo.SyncControlParams.SyncSkew.MinPixels = GSyncControlParams.syncSkew.minPixels;
			SyncTopo.SyncControlParams.SyncSkew.NumLines = GSyncControlParams.syncSkew.numLines;
			SyncTopo.SyncControlParams.SyncSkew.NumPixels = GSyncControlParams.syncSkew.numPixels;

			SyncTopo.SyncControlParams.StartupDelay.MaxLines = GSyncControlParams.startupDelay.maxLines;
			SyncTopo.SyncControlParams.StartupDelay.MinPixels = GSyncControlParams.startupDelay.minPixels;
			SyncTopo.SyncControlParams.StartupDelay.NumLines = GSyncControlParams.startupDelay.numLines;
			SyncTopo.SyncControlParams.StartupDelay.NumPixels = GSyncControlParams.startupDelay.numPixels;
		}

		SyncTopos.Emplace(SyncTopo);
	}
}
```

其消息封装为json格式,根据UE4生成元数据自动生成FString,接收端根据消息并反序列化成对应结构,生成对应处理的Task并返回一个接收到的消息.

``` c++
USTRUCT()
struct FSyncStatus
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSyncTopo> SyncTopos;

	UPROPERTY()
	TArray<FMosaicTopo> MosaicTopos;

	UPROPERTY()
	TArray<FString> FlipModeHistory;

	UPROPERTY()
	TArray<FString> ProgramLayers;

	UPROPERTY()
	uint32 DriverVersion;

	UPROPERTY()
	FString DriverBranch;

	UPROPERTY()
	FString Taskbar;

	UPROPERTY()
	uint32 PidInFocus;

	UPROPERTY()
	TArray<int8> CpuUtilization;

	UPROPERTY()
	uint64 AvailablePhysicalMemory;

	UPROPERTY()
	TArray<int8> GpuUtilization;

	UPROPERTY()
	TArray<int32> GpuCoreClocksKhz;

	UPROPERTY()
	TArray<int32> GpuTemperature;
};
// 结构转化JSON数据
FString CreateSyncStatusMessage(const FSyncStatus& SyncStatus)
{
	FString SyncStatusJsonString;
	const bool bJsonStringOk = FJsonObjectConverter::UStructToJsonObjectString(SyncStatus, SyncStatusJsonString);

	check(bJsonStringOk);

	FString Message;

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(TEXT("get sync status"), true); // TODO: Phase out this field and replace with two below because parser now needs to check all possibilities.
	JsonWriter->WriteValue(TEXT("command"), TEXT("get sync status"));
	JsonWriter->WriteValue(TEXT("bAck"), true);
	JsonWriter->WriteRawJSONValue(TEXT("syncStatus"), SyncStatusJsonString);
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();

	return Message;
}
// 发送消息
void FSwitchboardListener::SendMessageFutures()
{
	for (auto Iter = MessagesFutures.CreateIterator(); Iter; ++Iter)
	{
		FSwitchboardMessageFuture& MessageFuture = *Iter;

		if (!MessageFuture.Future.IsReady())
		{
			continue;
		}

		FString Message = MessageFuture.Future.Get();
		if (!Message.IsEmpty())
		{
			SendMessage(Message, MessageFuture.InEndpoint);
		}

		Iter.RemoveCurrent();
	}
}
bool FSwitchboardListener::SendMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint)
{
	if (Connections.Contains(InEndpoint))
	{
		TSharedPtr<FSocket> ClientSocket = Connections[InEndpoint];
		if (!ClientSocket.IsValid())
		{
			return false;
		}

		UE_LOG(LogSwitchboard, Verbose, TEXT("Sending message %s"), *InMessage);
		int32 BytesSent = 0;
		return ClientSocket->Send((uint8*)TCHAR_TO_UTF8(*InMessage), InMessage.Len() + 1, BytesSent);
	}

	// this happens when a client disconnects while a task it had issued is not finished
	UE_LOG(LogSwitchboard, Verbose, TEXT("Trying to send message to disconnected client %s"), *InEndpoint.ToString());
	return false;
}
// 处理接收消息,分析消息类型,根据消息类型具体处理
bool CreateTaskFromCommand(const FString& InCommand, const FIPv4Endpoint& InEndpoint, TUniquePtr<FSwitchboardTask>& OutTask, bool& bOutEcho)
{
    TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(InCommand);

	TSharedPtr<FJsonObject> JsonData;

	if (!FJsonSerializer::Deserialize(Reader, JsonData))
	{
		return false;
	}

	TSharedPtr<FJsonValue> CommandField = JsonData->TryGetField(TEXT("command"));
	TSharedPtr<FJsonValue> IdField = JsonData->TryGetField(TEXT("id"));

	if (!CommandField.IsValid() || !IdField.IsValid())
	{
		return false;
	}

	FGuid MessageID;

	if (!FGuid::Parse(IdField->AsString(), MessageID))
	{
		return false;
	}

	// Should we echo this command in the output log?
	{
		TSharedPtr<FJsonValue> EchoField = JsonData->TryGetField(TEXT("bEcho"));
		bOutEcho = EchoField.IsValid() ? EchoField->AsBool() : true;
	}

	const FString CommandName = CommandField->AsString().ToLower();
	...
	else if (CommandName == TEXT("get sync status"))
	{
		TSharedPtr<FJsonValue> UUIDField = TryGetCommandRequiredField(JsonData, TEXT("uuid"));

		FGuid ProgramID;
		if (UUIDField.IsValid() && FGuid::Parse(UUIDField->AsString(), ProgramID))
		{
			OutTask = MakeUnique<FSwitchboardGetSyncStatusTask>(MessageID, InEndpoint, ProgramID);
			return true;
		}
	}
	...
}
// 
bool FSwitchboardListener::ParseIncomingMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint)
{
	TUniquePtr<FSwitchboardTask> Task;
	bool bEcho = true;
	if (CreateTaskFromCommand(InMessage, InEndpoint, Task, bEcho))
	{
		if (Task->Type == ESwitchboardTaskType::Disconnect)
		{
			DisconnectTasks.Enqueue(MoveTemp(Task));
		}
		else if (Task->Type == ESwitchboardTaskType::KeepAlive)
		{
			LastActivityTime[InEndpoint] = FPlatformTime::Seconds();
		}
		else
		{
			if (bEcho)
			{
				UE_LOG(LogSwitchboard, Display, TEXT("Received %s command"), *Task->Name);
			}

			SendMessage(CreateCommandAcceptedMessage(Task->TaskID), InEndpoint);
			ScheduledTasks.Enqueue(MoveTemp(Task));
		}
		return true;
	}
}
```

switchboard使用python做为UI界面,其如何与C++端交互.(Engine\Plugins\VirtualProduction\Switchboard\Source\Switchboard\switchboard\devices\ndisplay)

``` python
# 截取一部分代码,获取'get sync status'消息如何处理
class DevicenDisplay(DeviceUnreal):
    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)
        self.unreal_client.delegates[
            'send file complete'] = self.on_send_file_complete
        self.unreal_client.delegates[
            'get sync status'] = self.on_get_sync_status
        self.unreal_client.delegates[
            'refresh mosaics'] = self.on_refresh_mosaics

    def on_get_sync_status(self, message):
        ''' Called when 'get sync status' is received. '''
        self.__class__.ndisplay_monitor.on_get_sync_status(
            device=self, message=message)
    @classmethod
    def create_monitor_if_necessary(cls):
        ''' Creates the nDisplay Monitor if it doesn't exist yet.
        '''
        if not cls.ndisplay_monitor:
            cls.ndisplay_monitor = nDisplayMonitor(None)

        return cls.ndisplay_monitor   
# nDisplayMonitor如何处理消息并显示
class nDisplayMonitor(QAbstractTableModel):
    def __init__(self, parent):
        QAbstractTableModel.__init__(self, parent)

        self.polling_period_ms = 1000

        # ordered so that we can map row indices to devices
        self.devicedatas = OrderedDict()

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll_sync_status)

        HEADER_DATA = {
            'Node': 'The cluster name of this device',
            'Host': 'The URL of the remote PC',
            'Connected': 'If we are connected to the listener of this device',
            'Driver': 'GPU driver version',
            'PresentMode':
                'Current presentation mode. Only available once the render '
                'node process is running. Expects "Hardware Composed: '
                'Independent Flip"',
            'Gpus': 'Informs if GPUs are synced.',
            'Displays': 'Detected displays and whether they are in sync',
            'SyncRate': 'Sync Frame Rate',
            'HouseSync':
                'Presence of an external sync signal connected to the remote '
                'Quadro Sync card',
            'SyncSource': 'The source of the GPU sync signal',
            'Mosaics': 'Display grids and their resolutions',
            'Taskbar':
                'Whether the taskbar is set to auto hide or always on top. It '
                'is recommended to be consistent across the cluster',
            'InFocus':
                'Whether nDisplay instance window is in focus. It is '
                'recommended to be in focus.',
            'ExeFlags':
                'It is recommended to disable fullscreen optimizations on the '
                'unreal executable. Only available once the render node '
                'process is running. Expects "DISABLEDXMAXIMIZEDWINDOWEDMODE"',
            'OsVer': 'Operating system version',
            'CpuUtilization':
                'CPU utilization average. The number of overloaded cores (>'
                f'{self.CORE_OVERLOAD_THRESH}% load) will be displayed in '
                'parentheses.',
            'MemUtilization': 'Physical memory, utilized / total.',
            'GpuUtilization':
                'GPU utilization. The GPU clock speed is displayed in '
                'parentheses.',
            'GpuTemperature':
                'GPU temperature in degrees celsius. (Max across all '
                'sensors.)',
        }

        self.colnames = list(HEADER_DATA.keys())
        self.tooltips = list(HEADER_DATA.values())    
    # 得到get sync status消息交给populate_sync_data处理
    def on_get_sync_status(self, device, message):
        '''
        Called when the listener has sent a message with the sync status
        '''
        try:
            if message['bAck'] is False:
                return
        except KeyError:
            LOGGER.error('Error parsing "get sync status" (missing "bAck")')
            return

        # Parse and update the model.
        deviceIdx, devicedata = self.devicedata_from_device(device)
        devicedata['time_last_update'] = time.time()
        devicedata['stale'] = False

        try:
            self.populate_sync_data(devicedata=devicedata, message=message)
        except (KeyError, ValueError):
            LOGGER.error(
                'Error parsing "get sync status" message and populating'
                'model data\n\n=== Traceback BEGIN ===\n'
                f'{traceback.format_exc()}=== Traceback END ===\n')
            return

        row = deviceIdx + 1
        self.dataChanged.emit(self.createIndex(row, 1),
                              self.createIndex(row, len(self.colnames)))
    # 分析消息并添加到devicedata['data'],其对应UI各栏的数据               
    def populate_sync_data(self, devicedata, message):
        '''
        Populates model data with message contents, which comes from 'get sync
        data' command.
        '''
        data = devicedata['data']
        device = devicedata['device']

        #
        # Sync Topology
        #
        sync_status = message['syncStatus']
        sync_topos = sync_status['syncTopos']

        # Build list informing which Gpus in each Sync group are in sync
        gpus = []

        for sync_topo in sync_topos:
            gpu_sync_oks = [gpu['bIsSynced'] for gpu in sync_topo['syncGpus']]
            gpu_syncs = map(lambda x: "Synced" if x else 'Free', gpu_sync_oks)
            gpus.append('%s' % (', '.join(gpu_syncs)))

        data['Gpus'] = '\n'.join(gpus) if len(gpus) > 0 else self.DATA_MISSING

        # Build list informing which Display in each Sync group are in sync.
        displays = []

        bpc_strings = {1: 6, 2: 8, 3: 10, 4: 12, 5: 16}

        for sync_topo in sync_topos:
            display_sync_states = [
                f"{syncDisplay['syncState']}"
                f"({bpc_strings.get(syncDisplay['bpc'], '??')}bpc)"
                for syncDisplay in sync_topo['syncDisplays']]
            displays.append(', '.join(display_sync_states))

        if len(displays) > 0:
            data['Displays'] = '\n'.join(displays)
        else:
            data['Displays'] = self.DATA_MISSING

        # Build Fps
        refresh_rates = \
            [f"{syncTopo['syncStatusParams']['refreshRate']*1e-4:.3f}"
                for syncTopo in sync_topos]

        if len(refresh_rates) > 0:
            data['Fps'] = '\n'.join(refresh_rates)
        else:
            data['Fps'] = self.DATA_MISSING

        # Build House Sync
        house_fpss = [syncTopo['syncStatusParams']['houseSyncIncoming']*1e-4
                      for syncTopo in sync_topos]
        house_syncs = [syncTopo['syncStatusParams']['bHouseSync']
                       for syncTopo in sync_topos]
        house_sync_fpss = list(map(lambda x: f"{x[1]:.3f}" if x[0] else 'no',
                               zip(house_syncs, house_fpss)))

        if len(house_sync_fpss) > 0:
            data['HouseSync'] = '\n'.join(house_sync_fpss)
        else:
            data['HouseSync'] = self.DATA_MISSING

        # Build Sync Source
        source_str = {0: 'Vsync', 1: 'House'}
        sync_sources = [sync_topo['syncControlParams']['source']
                        for sync_topo in sync_topos]
        sync_sources = [source_str.get(sync_source, 'Unknown')
                        for sync_source in sync_sources]
        bInternalSlaves = [sync_topo['syncStatusParams']['bInternalSlave']
                           for sync_topo in sync_topos]

        sync_slaves = []

        for i in range(len(sync_sources)):
            if bInternalSlaves[i] and sync_sources[i] == 'Vsync':
                sync_slaves.append('Vsync(daisy)')
            else:
                sync_slaves.append(sync_sources[i])

        if len(sync_slaves) > 0:
            data['SyncSource'] = '\n'.join(sync_slaves)
        else:
            data['SyncSource'] = self.DATA_MISSING

        # Mosaic Topology
        mosaic_topos = sync_status['mosaicTopos']

        mosaic_topo_lines = []

        for mosaic_topo in mosaic_topos:
            display_settings = mosaic_topo['displaySettings']
            width_per_display = display_settings['width']
            height_per_display = display_settings['height']

            width = mosaic_topo['columns'] * width_per_display
            height = mosaic_topo['rows'] * height_per_display

            # Ignoring displaySettings['freq'] because it seems to be fixed
            # and ignores sync frequency.
            line = f"{width}x{height} {display_settings['bpp']}bpp"
            mosaic_topo_lines.append(line)

        data['Mosaics'] = '\n'.join(mosaic_topo_lines)

        # Build PresentMode.
        flip_history = sync_status['flipModeHistory']

        if len(flip_history) > 0:
            data['PresentMode'] = flip_history[-1]

        # Detect PresentMode glitches
        if len(set(flip_history)) > 1:
            data['PresentMode'] = 'GLITCH!'
            data['TimeLastFlipGlitch'] = time.time()

        # Write time since last glitch
        if data['PresentMode'] != self.DATA_MISSING:
            time_since_flip_glitch = time.time() - data['TimeLastFlipGlitch']

            # For 1 minute, let the user know that there was a flip mode glitch
            if time_since_flip_glitch < 1*60:
                data['PresentMode'] = data['PresentMode'].split('\n')[0] \
                    + '\n' + str(int(time_since_flip_glitch))

        # Window in focus or not
        data['InFocus'] = 'no'
        for prg in device.program_start_queue.running_programs_named('unreal'):
            if prg.pid and prg.pid == sync_status['pidInFocus']:
                data['InFocus'] = 'yes'
                break

        # Show Exe flags (like Disable Fullscreen Optimization)
        data['ExeFlags'] = '\n'.join([
            layer for layer in sync_status['programLayers'][1:]])

        # Driver version
        try:
            driver = sync_status['driverVersion']
            data['Driver'] = f'{int(driver/100)}.{driver % 100}'
        except (KeyError, TypeError):
            data['Driver'] = self.DATA_MISSING

        # Taskbar visibility
        data['Taskbar'] = sync_status.get('taskbar', self.DATA_MISSING)

        # Operating system version
        data['OsVer'] = self.friendly_osver(device)

        # CPU utilization
        try:
            num_cores = len(sync_status['cpuUtilization'])
            num_overloaded_cores = 0
            cpu_load_avg = 0.0
            for core_load in sync_status['cpuUtilization']:
                cpu_load_avg += float(core_load) * (1.0 / num_cores)
                if core_load > self.CORE_OVERLOAD_THRESH:
                    num_overloaded_cores += 1

            data['CpuUtilization'] = f"{cpu_load_avg:.0f}%"
            if num_overloaded_cores > 0:
                data['CpuUtilization'] += f' ({num_overloaded_cores} cores >' \
                    f' {self.CORE_OVERLOAD_THRESH}%)'
        except (KeyError, ValueError):
            data['CpuUtilization'] = self.DATA_MISSING

        # Memory utilization
        try:
            gb = 1024 * 1024 * 1024
            mem_total = device.total_phys_mem
            mem_avail = sync_status.get('availablePhysicalMemory', 0)
            mem_utilized = mem_total - mem_avail
            data['MemUtilization'] = \
                f'{mem_utilized/gb:.1f} / {mem_total/gb:.0f} GB'
        except TypeError:
            data['MemUtilization'] = self.DATA_MISSING

        # GPU utilization + clocks
        try:
            gpu_stats = list(map(
                lambda x: f"#{x[0]}: {x[1]:.0f}% ({x[2] / 1000:.0f} MHz)",
                zip(count(), sync_status['gpuUtilization'],
                    sync_status['gpuCoreClocksKhz'])))

            if len(gpu_stats) > 0:
                data['GpuUtilization'] = '\n'.join(gpu_stats)
            else:
                data['GpuUtilization'] = self.DATA_MISSING
        except (KeyError, TypeError):
            data['GpuUtilization'] = self.DATA_MISSING

        # GPU temperature
        try:
            temps = [t if t != -2147483648 else self.DATA_MISSING
                     for t in sync_status['gpuTemperature']]

            if len(temps) > 0:
                data['GpuTemperature'] = '\n'.join(
                    map(lambda x: f"#{x[0]}: {x[1]}° C", zip(count(), temps)))
            else:
                data['GpuTemperature'] = self.DATA_MISSING
        except (KeyError, TypeError):
            data['GpuTemperature'] = self.DATA_MISSING

```

可以利用SwitchboardListener,获取消息并扩展处理.
