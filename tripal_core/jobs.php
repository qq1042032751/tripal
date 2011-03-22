<?php

/**
*
*
* @ingroup tripal_core
* @ingroup tripal_api
*/
function tripal_add_job ($job_name,$modulename,$callback,$arguments,$uid,
   $priority = 10){

   # convert the arguments into a string for storage in the database
   $args = implode("::",$arguments);

   $record = new stdClass();
   $record->job_name = $job_name;
   $record->modulename = $modulename;
   $record->callback = $callback;
   $record->status = 'Waiting';
   $record->submit_date = time();
   $record->uid = $uid;
   $record->priority = $priority;  # the lower the number the higher the priority
   if($args){
      $record->arguments = $args;
   }
   drupal_write_record('tripal_jobs',$record);
   $jobs_url = url("admin/tripal/tripal_jobs");
   drupal_set_message(t("Job '$job_name' submitted.  Check the <a href='$jobs_url'>jobs page</a> for status"));

   return 1;
}
/**
*   
*
* @ingroup tripal_core
* @ingroup tripal_api
*/
function tripal_job_set_progress($job_id,$percentage){

   if(preg_match("/^(\d\d|100)$/",$percentage)){
      $record = new stdClass();
      $record->job_id = $job_id; 
      $record->progress = $percentage;
	  if(drupal_write_record('tripal_jobs',$record,'job_id')){
	     return 1;
	  }
   }
   return 0;
}
/**
*   Returns a list of jobs associated with the given module
*
* @ingroup tripal_core
*/
function tripal_get_module_active_jobs ($modulename){
   $sql =  "SELECT * FROM {tripal_jobs} TJ ".
           "WHERE TJ.end_time IS NULL and TJ.modulename = '%s' ";
  return db_fetch_object(db_query($sql,$modulename));

}
/**
*
*
* @ingroup tripal_core
*/
function tripal_jobs_report () {
   //$jobs = db_query("SELECT * FROM {tripal_jobs} ORDER BY job_id DESC");
   $jobs = pager_query(
      "SELECT TJ.job_id,TJ.uid,TJ.job_name,TJ.modulename,TJ.progress,
              TJ.status as job_status, TJ,submit_date,TJ.start_time,
              TJ.end_time,TJ.priority
       FROM {tripal_jobs} TJ 
         INNER JOIN users U on TJ.uid = U.uid 
       ORDER BY job_id DESC", 10,0,"SELECT count(*) FROM {tripal_jobs}");
	
   // create a table with each row containig stats for 
   // an individual job in the results set.
   $output .= "Waiting jobs are executed first by priority level (the lower the ".
              "number the higher the priority) and second by the order they ".
              "were entered";
   $output .= "<table class=\"tripal-table tripal-table-horz\">". 
              "  <tr>".
              "    <th>Job ID</th>".
              "    <th>User</th>".
              "    <th>Job Name</th>".
              "    <th nowrap>Dates</th>".             
			     "    <th>Priority</th>".
			     "    <th>Progress</th>".
              "    <th>Status</th>".
              "    <th>Actions</th>".
              "  </tr>";
   $i = 0;
   while($job = db_fetch_object($jobs)){
      $class = 'tripal_feature-table-odd-row tripal-table-odd-row';
      if($i % 2 == 0 ){
         $class = 'tripal_feature-table-odd-row tripal-table-even-row';
      }
      $submit = format_date($job->submit_date);
      if($job->start_time > 0){
         $start = format_date($job->start_time);
      } else {
         if(strcmp($job->job_status,'Cancelled')==0){
            $start = 'Cancelled';
         } else {
            $start = 'Not Yet Started';
         }
      }
      if($job->end_time > 0){
         $end = format_date($job->end_time);
      } else {
         $end = '';
      }
      $cancel_link = '';
      if($job->start_time == 0 and $job->end_time == 0){
         $cancel_link = "<a href=\"".url("admin/tripal/tripal_jobs/cancel/".$job->job_id)."\">Cancel</a>";
      }
      $rerun_link = "<a href=\"".url("admin/tripal/tripal_jobs/rerun/".$job->job_id)."\">Re-run</a>";
      $output .= "  <tr $class>";
      $output .= "    <td>$job->job_id</td>".
                 "    <td>$job->name</td>".
                 "    <td>$job->job_name</td>".
                 "    <td nowrap>Submit Date: $submit".
                 "    <br>Start Time: $start".
                 "    <br>End Time: $end</td>".
                 "    <td>$job->priority</td>".
				     "    <td>$job->progress%</td>".
                 "    <td>$job->job_status</td>".
                 "    <td>$cancel_link $rerun_link</td>".
                 "  </tr>";
      $i++;
   }
   $output .= "</table>";
	$output .= theme_pager();
   return $output;
}
/**
*
*
* @ingroup tripal_core
*/
function tripal_jobs_launch (){
   
   // first check if any jobs are currently running
   // if they are, don't continue, we don't want to have
   // more than one job script running at a time
   if(tripal_jobs_check_running()){
      return;
   }
   
   // get all jobs that have not started and order them such that
   // they are processed in a FIFO manner. 
   $sql =  "SELECT * FROM {tripal_jobs} TJ ".
           "WHERE TJ.start_time IS NULL and TJ.end_time IS NULL ".
           "ORDER BY priority ASC,job_id ASC";
   $job_res = db_query($sql);
   while($job = db_fetch_object($job_res)){

		// set the start time for this job
		$record = new stdClass();
		$record->job_id = $job->job_id;
		$record->start_time = time();
		$record->status = 'Running';
		$record->pid = getmypid();
		drupal_write_record('tripal_jobs',$record,'job_id');

		// call the function provided in the callback column.
		// Add the job_id as the last item in the list of arguments. All
		// callback functions should support this argument.
		$callback = $job->callback;
		$args = split("::",$job->arguments);
		$args[] = $job->job_id;
		print "Calling: $callback(" . implode(", ",$args) . ")\n";   
		call_user_func_array($callback,$args);
		
		// set the end time for this job
		$record->end_time = time();
		$record->status = 'Completed';
		$record->progress = '100';
		drupal_write_record('tripal_jobs',$record,'job_id');
		
		// send an email to the user advising that the job has finished
   }
}
/**
*
*
* @ingroup tripal_core
* @ingroup tripal_api
*/
function tripal_jobs_check_running () {
   // iterate through each job that has not ended
   // and see if it is still running. If it is not
   // running but does not have an end_time then
   // set the end time and set the status to 'Error'
   $sql =  "SELECT * FROM {tripal_jobs} TJ ".
           "WHERE TJ.end_time IS NULL and NOT TJ.start_time IS NULL ";
   $jobs = db_query($sql);
   while($job = db_fetch_object($jobs)){
      if($job->pid and posix_kill($job->pid, 0)) {
         // the job is still running so let it go
		   // we return 1 to indicate that a job is running
		   print "Job is still running (pid $job->pid)\n";
		   return 1;
      } else {
	      // the job is not running so terminate it
	      $record = new stdClass();
         $record->job_id = $job->job_id;
	      $record->end_time = time();
         $record->status = 'Error';
	      $record->error_msg = 'Job has terminated unexpectedly.';
         drupal_write_record('tripal_jobs',$record,'job_id');
	   }
   }
   // return 1 to indicate that no jobs are currently running.
   return 0;
}

/**
*
* @ingroup tripal_core
* @ingroup tripal_api
*/
function tripal_jobs_rerun ($job_id){
   global $user;

   $sql = "select * from {tripal_jobs} where job_id = %d";
   $job = db_fetch_object(db_query($sql,$job_id));

   $args = explode("::",$job->arguments);
   tripal_add_job ($job->job_name,$job->modulename,$job->callback,$args,$user->uid,
      $job->priority);

   drupal_goto("admin/tripal/tripal_jobs");
}

/**
*
* @ingroup tripal_core
* @ingroup tripal_api
*/
function tripal_jobs_cancel ($job_id){
   $sql = "select * from {tripal_jobs} where job_id = %d";
   $job = db_fetch_object(db_query($sql,$job_id));

   // set the end time for this job
   if($job->start_time == 0){
      $record = new stdClass();
      $record->job_id = $job->job_id;
	   $record->end_time = time();
	   $record->status = 'Cancelled';
	   $record->progress = '0';
	   drupal_write_record('tripal_jobs',$record,'job_id');
      drupal_set_message("Job #$job_id cancelled");
   } else {
      drupal_set_message("Job #$job_id cannot be cancelled. It is in progress or has finished.");
   }
   drupal_goto("admin/tripal/tripal_jobs");
}
