*condor_gpu_discovery*
========================

Output GPU-related ClassAd attributes
:index:`condor_gpu_discovery<single: condor_gpu_discovery; HTCondor commands>`\ :index:`condor_gpu_discovery command`

Synopsis
--------

**condor_gpu_discovery** **-help**

**condor_gpu_discovery** [**<options>** ]

Description
-----------

*condor_gpu_discovery* outputs ClassAd attributes corresponding to a
host's GPU capabilities. It can presently report CUDA and OpenCL
devices; which type(s) of device(s) it reports is determined by which
libraries, if any, it can find when it runs; this reflects what GPU jobs
will find on that host when they run. (Note that some HTCondor
configuration settings may cause the environment to differ between jobs
and the HTCondor daemons in ways that change library discovery.)

If ``CUDA_VISIBLE_DEVICES`` or ``GPU_DEVICE_ORDINAL`` is set in the
environment when *condor_gpu_discovery* is run, it will report only
devices present in the those lists.

This tool is not available for MAC OS platforms.

With no command line options, the single ClassAd attribute
``DetectedGPUs`` is printed. If the value is 0, no GPUs were detected.
If one or more GPUS were detected, the value is a string, presented as a
comma and space separated list of the GPUs discovered, where each is
given a name further used as the *prefix string* in other attribute
names. Where there is more than one GPU of a particular type, the
*prefix string* includes an integer value numbering the device; these
integer values monotonically increase from 0 unless the ``-uuid`` or ``-short-uuid``
option is used or unless otherwise specified
in the environment; see above. For example, a discovery of two GPUs may
output

.. code-block:: condor-classad

    DetectedGPUs="CUDA0, CUDA1"

Further command line options use ``"CUDA"`` either with or without one
of the integer values 0 or 1 as the *prefix string* in attribute names.

For machines with more than one or two NVIDIA devices, it is recommended that you
also use the ``-short-uuid`` or ``-uuid`` option.  The uuid value assigned by
NVIDA to each GPU is unique, so  using this option provides stable device
identifiers for your devices. The ``--short-uuid`` option uses only part of the
uuid, but it is highly likely to still be unique for devices on a single machine.
When ``-short-uuid`` is used, discovery of two GPUs may look like this

.. code-block:: condor-classad

    DetectedGPUs="GPU-ddc1c098, GPU-9dc7c6d6"

Any NVIDA runtime library later than 9.0 will accept the above identifiers in the
``CUDA_VISIBLE_DEVICES`` environment variable.

Options
-------

 **-help**
    Print usage information and exit.
 **-properties**
    In addition to the ``DetectedGPUs`` attribute, display some of the
    attributes of the GPUs. Each of these attributes will have a *prefix
    string* at the beginning of its name. The displayed CUDA attributes
    are ``Capability``, ``DeviceName``, ``DriverVersion``,
    ``ECCEnabled``, ``GlobalMemoryMb``, and ``RuntimeVersion``. The
    displayed Open CL attributes are ``DeviceName``, ``ECCEnabled``,
    ``OpenCLVersion``, and ``GlobalMemoryMb``.
 **-extra**
    Display more attributes of the GPUs. Each of these attribute names
    will have a *prefix string* at the beginning of its name. The
    additional CUDA attributes are ``ClockMhz``, ``ComputeUnits``, and
    ``CoresPerCU``. The additional Open CL attributes are ``ClockMhz``
    and ``ComputeUnits``.
 **-dynamic**
    Display attributes of NVIDIA devices that change values as the GPU
    is working. Each of these attribute names will have a *prefix
    string* at the beginning of its name. These are ``FanSpeedPct``,
    ``BoardTempC``, ``DieTempC``, ``EccErrorsSingleBit``, and
    ``EccErrorsDoubleBit``.
 **-mixed**
    When displaying attribute values, assume that the machine has a
    heterogeneous set of GPUs, so always include the integer value in
    the *prefix string*.
 **-device** *<N>*
    Display properties only for GPU device *<N>*, where *<N>* is the
    integer value defined for the *prefix string*. This option may be
    specified more than once; additional *<N>* are listed along with the
    first. This option adds to the devices(s) specified by the
    environment variables ``CUDA_VISIBLE_DEVICES`` and
    ``GPU_DEVICE_ORDINAL``, if any.
 **-tag** *string*
    Set the resource tag portion of the intended machine ClassAd
    attribute ``Detected<ResourceTag>`` to be *string*. If this option
    is not specified, the resource tag is ``"GPUs"``, resulting in
    attribute name ``DetectedGPUs``.
 **-prefix** *str*
    When naming attributes, use *str* as the *prefix string*. When this
    option is not specified, the *prefix string* is either ``CUDA`` or
    ``OCL`` unless ``-uuid`` or ``-short-uuid`` is also used.
 **-short-uuid**
    Use the first 8 characters of the NVIDIA uuid as the device identifier.
    When this option is used, devices will be shown as ``GPU-<xxxxxxxx>`` where
    <xxxxxxxx> is the first 8 hex digits of the NVIDIA device uuid.  Unlike device
    indices, the uuid of a device will not change of other devices are taken offline
    or drained.
 **-uuid**
    Use the full NVIDIA uuid as the device identifier rather than the device index.
 **-simulate:D,N**
    For testing purposes, assume that N devices of type D were detected.
    No discovery software is invoked. If D is 0, it refers to GeForce GT
    330, and a default value for N is 1. If D is 1, it refers to GeForce
    GTX 480, and a default value for N is 2.
 **-opencl**
    Prefer detection via OpenCL rather than CUDA. Without this option,
    CUDA detection software is invoked first, and no further Open CL
    software is invoked if CUDA devices are detected.
 **-cuda**
    Do only CUDA detection.
 **-nvcuda**
    For Windows platforms only, use a CUDA driver rather than the CUDA
    run time.
 **-config**
    Output in the syntax of HTCondor configuration, instead of ClassAd
    language. An additional attribute is produced ``NUM_DETECTED_GPUs``
    which is set to the number of GPUs detected.
 **-repeat** [*N*]
    Repeat listed GPUs *N* (default 2) times.  This results in a list
    that looks like ``CUDA0, CUDA1, CUDA0, CUDA1``.
 **-packed**
    When repeating GPUs, repeat each GPU *N* times, not the whole list.
    This results in a list that looks like ``CUDA0, CUDA0, CUDA1, CUDA1``.
 **-cron**
    This option suppresses the ``DetectedGpus`` attribute so that the
    output is suitable for use with *condor_startd* cron. Combine this
    option with the **-dynamic** option to periodically refresh the
    dynamic Gpu information such as temperature. For example, to refresh
    GPU temperatures every 5 minutes
 **-verbose**
    Also print detection progress. This option is for interactive use only.

    .. code-block:: condor-config

        use FEATURE : StartdCronPeriodic(DYNGPUS, 5*60, $(LIBEXEC)/condor_gpu_discovery, -dynamic -cron)
          

 **-verbose**
    For interactive use of the tool, output extra information to show
    detection while in progress.
 **-diagnostic**
    Show diagnostic information, to aid in tool development.

Exit Status
-----------

*condor_gpu_discovery* will exit with a status value of 0 (zero) upon
success, and it will exit with the value 1 (one) upon failure.

