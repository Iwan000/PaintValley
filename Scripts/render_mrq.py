"""Project Rain - renders LS_RainTrick with Movie Render Queue (PIE executor) to a JPG sequence.
Raindrop motion blur is integrated analytically in the drop material, so MRQ uses 1 temporal sample
(extra temporal samples would blur the streaks twice). Usage inside the editor:
    exec(open(".../render_mrq.py").read()); render(start_s=0, end_s=46, name="RainTrick")
"""
import unreal

ROOT = "/Game/RainMagic"
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def render(start_s=0.0, end_s=46.0, name="RainTrick", width=1920, height=804, spatial=2, cvars=None):
    # The controller's timeline must start where the render starts (it advances frame-locked from BeginPlay).
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == "RainMagicController":
            a.set_editor_property("start_time", float(start_s))
            a.set_editor_property("loop", False)
            a.set_editor_property("pixel_scale_override", 0.0)
            a.set_editor_property("render_width_override", float(width))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    subsystem = unreal.get_editor_subsystem(unreal.MoviePipelineQueueSubsystem)
    queue = subsystem.get_queue()
    queue.delete_all_jobs()
    job = queue.allocate_new_job(unreal.MoviePipelineExecutorJob)
    job.job_name = name
    job.sequence = unreal.SoftObjectPath(f"{ROOT}/Cinematics/LS_RainTrick.LS_RainTrick")
    job.map = unreal.SoftObjectPath(f"{ROOT}/Maps/L_RainStage.L_RainStage")
    cfg = job.get_configuration()

    out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
    out.output_resolution = unreal.IntPoint(width, height)
    out.output_directory = unreal.DirectoryPath(PROJECT + "Saved/MovieRenders/" + name)
    out.file_name_format = "{sequence_name}.{frame_number}"
    out.use_custom_playback_range = True
    out.custom_start_frame = int(round(start_s * 24000 / 1001))
    out.custom_end_frame = int(round(end_s * 24000 / 1001))
    out.override_existing_output = True

    cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
    jpg = cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_JPG)

    aa = cfg.find_or_add_setting_by_class(unreal.MoviePipelineAntiAliasingSetting)
    aa.spatial_sample_count = spatial
    aa.temporal_sample_count = 1
    aa.override_anti_aliasing = False
    aa.engine_warm_up_count = 0
    aa.render_warm_up_count = 8
    aa.use_camera_cut_for_warm_up = False

    # Extra console variables for this render only, e.g. cvars={"r.DepthOfFieldQuality": 2}. The real-time setting of
    # TSR's history at the output resolution (Config/DefaultEngine.ini) goes back to the Epic level's 200 % for movies.
    cv = cfg.find_or_add_setting_by_class(unreal.MoviePipelineConsoleVariableSetting)
    merged = {"r.TSR.History.ScreenPercentage": 200}
    merged.update(cvars or {})
    for k, v in merged.items():
        cv.add_or_update_console_variable(k, v)

    executor = unreal.MoviePipelinePIEExecutor()
    subsystem.render_queue_with_executor_instance(executor)
    print("MRQ render started:", name, out.custom_start_frame, "->", out.custom_end_frame)
    return executor
