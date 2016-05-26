<?php
/**
 * Retrieves a TripalTerm entity that matches the given arguments.
 *
 * @param $values
 *   An associative array used to match a term.  Valid keys may be 'vocabulary',
 *   'accession, or 'term_id'.  The keys 'vocabulary' and 'accession' must
 *   always be used together to uniquely identify a term.  The key 'term_id'
 *   can be used alone to uniquely identify a term.
 *
 * @return
 *   A TripalTerm entity object or NULL if not found.
 */
function tripal_load_term_entity($values) {
  $vocabulary = array_key_exists('vocabulary', $values) ? $values['vocabulary'] : '';
  $accession = array_key_exists('accession', $values) ? $values['accession'] : '';
  $term_id = array_key_exists('term_id', $values) ? $values['term_id'] : '';

  $term = NULL;

  if ($vocabulary and $accession) {
    $query = db_select('tripal_term', 'tt');
    $query->join('tripal_vocab', 'tv', 'tv.id = tt.vocab_id');
    $query->fields('tt', array('id'))
      ->fields('tv', array('vocabulary'))
      ->condition('tv.vocabulary', $vocabulary)
      ->condition('tt.accession', $accession);
    $term = $query->execute()->fetchObject();
  }
  else if ($term_id) {
    $query = db_select('tripal_term', 'tt');
    $query->fields('tt', array('id'))
      ->condition('tt.id', $term_id);
    $term = $query->execute()->fetchObject();
  }

  if ($term) {
    $entity = entity_load('TripalTerm', array($term->id));
    return reset($entity);
  }
  return NULL;
}

/**
 * Retrieves a TripalVocab entity that maches the given arguments.
 *
 * @param $values
 *   An associative array used to match a vocabulary.  The valid keys are
 *   'vocab_id' and 'vocabulary'.
 *
 * @return
 * A TripalVocab entity object or NULL if not found.
 */
function tripal_load_vocab_entity($values) {
  $vocabulary = array_key_exists('vocabulary', $values) ? $values['vocabulary'] : '';
  $vocab_id = array_key_exists('vocab_id', $values) ? $values['vocab_id'] : '';
  $vocab = NULL;

  $query= db_select('tripal_vocab', 'tv')
    ->fields('tv');

  if ($vocabulary) {
    $query->condition('tv.vocabulary', $vocabulary);
  }
  if ($vocab_id) {
    $query->condition('tv.id', $vocab_id);
  }
  $vocab = $query->execute()->fetchObject();

  if ($vocab) {
    $entity = entity_load('TripalVocab', array($vocab->id));
    return reset($entity);
  }
  return NULL;
}

/**
 * Retrieves a TripalBundle entity that matches the given arguments.
 *
 * @param $values
 *   An associative array used to match a bundle.  Valid keys may:
 *     - id: the numeric id of the bundle.
 *     - name:  the bundle name (e.g. 'bio_data_234')
 *     - label: the bundle label (e.g. 'Organism')
 *     - term_id: the term ID to which the bundle belongs
 *
 * @return
 *   A TripalBundle entity object or NULL if not found.
 */
function tripal_load_bundle_entity($values) {

  $query = db_select('tripal_bundle', 'tb');
  $query->fields('tb');
  if (array_key_exists('id', $values)) {
    $query->condition('tb.id', $values['id']);
  }
  if (array_key_exists('name', $values)) {
    $query->condition('tb.name', $values['name']);
  }
  if (array_key_exists('label', $values)) {
    $query->condition('tb.label', $values['label']);
  }
  if (array_key_exists('term_id', $values)) {
    $query->condition('tb.term_id', $values['term_id']);
  }
  $bundle = $query->execute()->fetchObject();

  if ($bundle) {
    $entity = entity_load('TripalBundle', array($bundle->id));
    return reset($entity);
  }
  return NULL;
}
/**
 * Creates a new Tripal Entity type (i.e. bundle).
 *
 * @param $vocabulary
 *   The abbreviated vocabulary for the vocabulary (e.g. RO, SO, PATO).
 * @param $accession
 *   The unique term ID in the vocabulary $vocabulary (i.e. an accession).
 * @param $term_name
 *   A human-readable name for this term.  This will became the name that
 *   appears for the content type.  In practice, this should be the name
 *   of the term. (E.g. the name for SO:0000704 is gene).
 * @param $error
 *  A string, passed by reference, that is filled with the error message
 *  if the function fails.
 *
 * @return
 *  TRUE if the entity type (bundle) was succesfully created.  FALSE otherwise.
 */
function tripal_create_bundle($vocabulary, $accession, $term_name, &$error = '') {

  // First create the TripalVocab if it doesn't already exist.
  $vocab = tripal_load_vocab_entity(array('vocabulary' => $vocabulary));
  if (!$vocab) {
    $vocab = entity_get_controller('TripalVocab')->create(array('vocabulary' => $vocabulary));
    $vocab->save();
  }

  // Next create the TripalTerm if it doesn't already exist.
  $term = tripal_load_term_entity(array(
    'vocabulary' => $vocabulary,
    'accession' => $accession
  ));
  if (!$term) {
    $args = array('vocab_id' => $vocab->id, 'accession' => $accession, 'name' => $term_name);
    $term = entity_get_controller('TripalTerm')->create($args);
    $term = $term->save();
  }

  // If the bundle doesn't already exist, then add it.
  $bundle_name = 'bio_data_' . $term->id;
  $einfo = entity_get_info('TripalEntity');
  if (!in_array($bundle_name, array_keys($einfo['bundles']))) {
    // Insert the bundle.
    db_insert('tripal_bundle')
      ->fields(array(
        'label' => $term_name,
        'type' => 'TripalEntity',
        'name' => $bundle_name,
        'term_id' => $term->id,
      ))
      ->execute();
  }

  // Clear the entity cache so that Drupal will read our
  // hook_entity_info() implementation.
  global $language;
  $langcode = $language->language;
  cache_clear_all("entity_info:$langcode", 'cache');
  variable_set('menu_rebuild_needed', TRUE);

  // Get the bundle object.
  $bundle = tripal_load_bundle_entity(array('name' => $bundle_name));

  // Allow modules now add fields to the bundle
  module_invoke_all('add_bundle_fields', 'TripalEntity', $bundle, $term);

  return TRUE;
}

/**
 * Refreshes the bundle such that new fields added by modules will be found.
 *
 * @param $bundle_name
 *   The name of the bundle to refresh (e.g. bio_data_4).
 */
function tripal_refresh_bundle_fields($bundle_name) {
  // Get the bundle object.
  $bundle = tripal_load_bundle_entity(array('name' => $bundle_name));
  if (!$bundle) {
    tripal_report_error('tripal', TRIPAL_ERROR, "Unrecognized bundle name '%bundle'.",
        array('%bundle' => $bundle_name));
    return FALSE;
  }
  $term = tripal_load_term_entity(array('term_id' => $bundle->term_id));

  // Allow modules now add fields to the bundle
  module_invoke_all('add_bundle_fields', 'TripalEntity', $bundle, $term);

  // Allow modules to update existing fields
  module_invoke_all('update_bundle_fields', 'TripalEntity', $bundle, $term);
}


/**
 * Adds a new field and attaches it to a bundle
 *
 * @param $field_name
 *   The name of the field.
 * @param $field_info
 *   An associative array containing the field information.  The following
 *   key/value pairs are supported:
 *     'field_type' : a valid field type.  May be any of the Drupal default
 *       fields, one created by the tripal_chado module or another custom module.
 *     'widget_type' : a valid widget type. May be any of the Drupal default
 *       fields, one created by the tripal_chado module or another custom module.
 *     'field_settings' : an array of settings that are appropriate for the
 *       selected field type.
 *     'widget_settings' : an array of settings that are appropriate for the
 *       selected widget type.
 *     'description' :  a default description for this field.
 *     'label' : a label used as a header for this field.
 *     'is_required' : indicates if the field is required in the edit form.
 *     'cardinality' : indicates the number of values this field can support.
 *       the default is 1 (meaning only one value). Use a value of
 *       FIELD_CARDINALITY_UNLIMITED for unlimited number of values.
 *     'default_value' : A default value for the field.
 *     'format' : A string indicating the format for the field. Must be
 *       specific to the field.
 * @param $entity_type_name
 *   The entity type name.
 * @param $bundle_name
 *   The bundle name.
 *
 * TODO: this function really shouldn't try to create an instance and
 * attach to a bundle  at the same time.
 */
function tripal_add_bundle_field($field_name, $field_info, $entity_type_name, $bundle_name) {

  $field = field_info_field($field_name);

  // If the field exists and is attached to this bundle then just return,
  // there is nothing left to do.
  if ($field and array_key_exists('bundles', $field) and
      array_key_exists($entity_type_name, $field['bundles']) and
      in_array($bundle_name, $field['bundles'][$entity_type_name])) {
    return;
  }

  $cardinality = 1;
  if (array_key_exists('cardinality', $field_info) and is_numeric($field_info['cardinality'])) {
    $cardinality = $field_info['cardinality'];
  }

  // If the field doesn't exist then create it.
  if (!$field) {
    $field = array(
      'field_name' => $field_name,
      'type' => $field_info['field_type'],
      'cardinality' => $cardinality,
      'locked' => FALSE,
      'storage' => array(
        'type' => $field_info['storage']
      ),
      'settings' => $field_info['field_settings'],
    );
    field_create_field($field);
  }

  // Attach the field to the bundle.
  $field_instance = array(
    'field_name' => $field_name,
    'label' => $field_info['label'],
    'description' => $field_info['description'],
    'widget' => array(
      'type' => $field_info['widget_type'],
      'settings' => $field_info['widget_settings'],
    ),
    'entity_type' => $entity_type_name,
    'required' => $field_info['is_required'],
    'settings' => $field_info['field_settings'],
    'bundle' => $bundle_name,
    'default_value' => array_key_exists('default_value', $field_info) ? $field_info['default_value'] : '',
    'format' => array_key_exists('format', $field_info) ? $field_info['format'] : '',
  );
  field_create_instance($field_instance);
}

/**
 * Updates an existing field and its attached instance to a bundle.
 *
 *
 * @param $field_name
 *   The name of the field.
 * @param $field_info
 *   An associative array containing the field information.  The following
 *   key/value pairs are supported:
 *     'field_type' : a valid field type.  May be any of the Drupal default
 *       fields, one created by the tripal_chado module or another custom module.
 *     'widget_type' : a valid widget type. May be any of the Drupal default
 *       fields, one created by the tripal_chado module or another custom module.
 *     'field_settings' : an array of settings that are appropriate for the
 *       selected field type.
 *     'widget_settings' : an array of settings that are appropriate for the
 *       selected widget type.
 *     'description' :  a default description for this field.
 *     'label' : a label used as a header for this field.
 *     'is_required' : indicates if the field is required in the edit form.
 *     'cardinality' : indicates the number of values this field can support.
 *       the default is 1 (meaning only one value). Use a value of
 *       FIELD_CARDINALITY_UNLIMITED for unlimited number of values.
 *     'default_value' : A default value for the field.
 *     'format' : A string indicating the format for the field. Must be
 *       specific to the field.
 * @param $entity_type_name
 *   The entity type name.
 * @param $bundle_name
 *   The bundle name.
 *
 * @return
 *   FALSE if the field could not be updated
 *
 * TODO: this function really shouldn't try to create an instance and
 * attach to a bundle  at the same time.
 *
 */
function tripal_update_bundle_field($field_name, $field_info, $entity_type_name, $bundle_name) {

  $field = field_info_field($field_name);

  // If the field doesn't exists or is not attached to this bundle then
  // just return, there is nothing left to do.
  if (!$field or !array_key_exists('bundles', $field) or
      !array_key_exists($entity_type_name, $field['bundles']) or
      !in_array($bundle_name, $field['bundles'][$entity_type_name])) {
    return FALSE;
  }

  $field['field_name'] = $field_name;
  if (array_key_exists('field_type', $field_info)) {
    $field['cardinality'] = $field_info['cardinality'];
  }
  if (array_key_exists('locked', $field_info)) {
    $field['locked'] = $field_info['locked'];
  }
  if (array_key_exists('storage', $field_info)) {
    $field['storage']['type'] = $field_info['storage'];
  }
  if (array_key_exists('field_settings', $field_info)) {
    $field['settings'] = $field_info['field_settings'];
  }

  field_update_field($field);

  $field_instance['field_name'] = $field_name;
  $field_instance['entity_type'] = $entity_type_name;
  $field_instance['bundle'] = $bundle_name;
  if (array_key_exists('label', $field_info)) {
    $field['label'] = $field_info['label'];
  }
  if (array_key_exists('description', $field_info)) {
    $field['description'] = $field_info['description'];
  }
  if (array_key_exists('widget', $field_info)) {
    if (array_key_exists('widget_type', $field_info['widget'])) {
      $field['widget']['type'] = $field_info['widget_type'];
    }
    if (array_key_exists('widget_settings', $field_info['widget'])) {
      $field['widget']['settings'] = $field_info['widget_settings'];
    }
  }
  if (array_key_exists('required', $field_info)) {
    $field['required'] = $field_info['is_required'];
  }
  if (array_key_exists('settings', $field_info)) {
    $field['settings'] = $field_info['field_settings'];
  }
  if (array_key_exists('default_value', $field_info)) {
    $field['default_value'] = $field_info['default_value'];
  }
  if (array_key_exists('format', $field_info)) {
    $field['format'] = $field_info['format'];
  }
  field_update_instance($field_instance);
}

/**
 * Allows a module to make changes to an entity object after creation.
 *
 * This function is added by Tripal to allow datastore backends to add
 * addition properties to the entity that they themselves will use later.
 *
 * @param $entity
 * @param $entity_type
 */
function hook_entity_create(&$entity, $entity_type) {

}


/**
 * Adds fields to a bundle type.
 *
 * When a new bundle (Tripal content type) is created, the tripal module
 * allows any other module the implements this hook to add fields too it. This
 * hook is called automatically.
 *
 * @param $entity_type
 *   The type of entity (e.g. 'TripalEntity')
 * @param $bundle
 *   An instance of a TripalBundle object.  This is the bundle to which
 *   the fields will be added.
 * @param $term
 *   An instance of a TripalTerm object. Each TripalBundle is associated with
 *   a controlled vocabulary term. This is the term object for the bundle.
 *
 * @return
 *   There is no return value.
 */
function hook_add_bundle_fields($entity_type, $bundle, $term) {

  //
  // The example code below is derived from the tripal_chado modules and
  // adds a 'synonym' field to the bundle.
  //

  $bundle_name = $bundle->name;

  // This array will hold details that map the bundle to tables in Chado.
  $bundle_data = array();

  // Get the cvterm that corresponds to this TripalTerm object.
  $vocab = entity_load('TripalVocab', array($term->vocab_id));
  $vocab = reset($vocab);
  $match = array(
    'dbxref_id' => array(
      'db_id' => array(
        'name' => $vocab->vocabulary,
      ),
      'accession' => $term->accession
    ),
  );
  $cvterm = chado_generate_var('cvterm', $match);


  // Get the default table that this term maps to.
  $default = db_select('tripal_cv_defaults', 't')
  ->fields('t')
  ->condition('cv_id', $cvterm->cv_id->cv_id)
  ->execute()
  ->fetchObject();
  if ($default) {
    $bundle_data = array(
      'cv_id' => $cvterm->cv_id->cv_id,
      'cvterm_id' => $cvterm->cvterm_id,
      'data_table' => $default->table_name,
      'type_table' => $default->table_name,
      'field' =>  $default->field_name,
    );
  }
  else {
    return;
  }

  // Formulate the name of the synonym table.
  $syn_table = $bundle_data['data_table'] . '_synonym';

  // Don't add a field to this bundle if
  if (!chado_table_exists($syn_table)) {
    return;
  }

  $field_name = $syn_table;
  $schema = chado_get_schema($syn_table);
  $pkey = $schema['primary key'][0];

  // The Tripal Chado module needs to know what Chado table and field
  // this field maps to, as well as the base table that it maps to
  // if this field will map to a record in a linking table.
  $field_settings = array(
    // The Chado table that this field maps to.
    'chado_table' => $syn_table,
    // The column in the chado table that this field maps to.
    'chado_column' => $pkey,
    // The base table that this field is connected to.
    'base_table' => $base_table,
    'semantic_web' => '',
  );

  // Finally set the field values.
  $field_info = array(
    'field_type' => 'synonym',
    'widget_type' => 'tripal_fields_synonym_widget',
    'widget_settings' => array('display_label' => 1),
    'description' => '',
    'label' => 'Synonyms',
    'is_required' => 0,
    'cardinality' => FIELD_CARDINALITY_UNLIMITED,
    'storage' => 'field_chado_storage',
    'field_settings' => $field_settings,
  );

  // Call the Tripal API function tripal_add_bundle_field to complete the
  // addition of the field to the bundle.
  tripal_add_bundle_field($field_name, $field_info, $entity_type_name, $bundle_name);
}

/**
 * @section
 * Bundle Variables.
 */

/**
 * Fetch the value for a given tripal variable.
 *
 * @param string $variable_name
 *   The name of the variable as in tripal_variables.name.
 * @param int $bundle_id
 *   The unique identfier for the bundle you want the value for.
 * @return text
 *   The value of the specified variable for the specified bundle.
 */
function tripal_get_bundle_variable($variable_name, $bundle_id, $default = FALSE) {

  $variable = tripal_get_variable($variable_name);

  // Warn if we can't find the tripal_variable.
  if (!$variable) {
    return $default;
  }

  // Select the value for this variable.
  $value = db_select('tripal_bundle_variables', 'var')
    ->fields('var', array('value'))
    ->condition('var.bundle_id', $bundle_id)
    ->condition('var.variable_id', $variable->variable_id)
    ->execute()
    ->fetchField();

  // Warn if the value appears to be empty.
  if (!$value) {
    return $default;
  }

  return $value;
}

/**
 * Save the value of a tripal variable for a given bundle.
 *
 * @param string $variable_name
 *   The name of the variable as in tripal_variables.name.
 * @param int $bundle_id
 *   The unique identfier for the bundle you want the value for.
 * @param $text $value
 *   The value of the variable for the given bundle.
 */
function tripal_set_bundle_variable($variable_name, $bundle_id, $value) {

  $variable = tripal_get_variable($variable_name);

  if (!$variable) {
    return FALSE;
  }

  // And then we need to write the new format to the tripal_bundle_variables table.
  $record = array(
    'bundle_id' => $bundle_id,
    'variable_id' => $variable->variable_id,
    'value' => $value,
  );

  // Check whether there is already a format saved.
  $bundle_variable_id = db_select('tripal_bundle_variables', 'var')
    ->fields('var', array('bundle_variable_id'))
    ->condition('var.bundle_id', $record['bundle_id'])
    ->condition('var.variable_id', $record['variable_id'])
    ->execute()
    ->fetchField();
  if ($bundle_variable_id) {
    $record['bundle_variable_id'] = $bundle_variable_id;
    return drupal_write_record('tripal_bundle_variables', $record, 'bundle_variable_id');
  }
  else {
    return drupal_write_record('tripal_bundle_variables', $record);
  }

}


/**
 * @section
 * Title & URL Formats.
 */

/**
 * Get Page Title Format for a given Tripal Entity Type.
 *
 * @param TripalBundle $entity
 *   The Entity object for the Tripal Bundle the title format is for.
 */
function tripal_get_title_format($entity) {

  // Get the existing title format if it exists.
  $title_format = tripal_get_bundle_variable('title_format', $entity->id);

  // If there isn't yet a title format for this bundle/type then we should
  // determine the default.
  if (!$title_format) {
    $title_format = tripal_get_default_title_format($entity);
    tripal_save_title_format($entity, $title_format);
  }

  return $title_format;
}

/**
 * Save Page Title Format for a given Tripal Entity Type.
 *
 * @param TripalBundle $entity
 *   The Entity object for the Tripal Bundle the title format is for.
 * @param string $format
 *   The pattern to be used when generating entity titles for the above type.
 */
function tripal_save_title_format($entity, $format) {

  return tripal_set_bundle_variable('title_format', $entity->id, $format);
}

/**
 * Determine the default title format to use for an entity.
 *
 * @param TripalBundle $entity
 *   The Entity object for the Tripal Bundle that the title format is for.
 *
 * @return string
 *   A default title format.
 */
function tripal_get_default_title_format($entity) {
  $format = NULL;

  // Retrieve all available tokens.
  $tokens = tripal_get_entity_tokens($entity);

  // A) Check to see if more informed modules have suggested a title for this type.
  // Invoke hook_tripal_default_title_format() to get all suggestions from other modules.
  $suggestions = module_invoke_all('tripal_default_title_format', $entity, $tokens);
  if ($suggestions) {
    // Use the suggestion with the lightest weight.
    $lightest_key = NULL;
    foreach ($suggestions as $k => $s) {
      if ($lightest_key === NULL) $lightest_key = $k;
      if ($s['weight'] < $lightest_key) $lightest_key = $k;
    }
    $format = $suggestions[$lightest_key]['format'];
  }

  // B) Check to see if any fields contain "name" in the machine name and if so, use them.
  $name_fields = preg_grep('/name/', array_keys($tokens));
  if ($name_fields AND !$format) {
    $format = implode(', ', $name_fields);
  }

  // C) Generate our own ugly title by simply comma-separating all the required fields.
  if (!$format) {
    $tmp = array();

    // Check which tokens are required fields and join them into a default format.
    foreach($tokens as $token) {
      if ($token['required']) {
        $tmp[] = $token['token'];
      }
    }
    $format = implode(', ', $tmp);
  }

  return $format;
}

/**
 * Implement this hook to define default formats for Tripal Content Types.
 *
 * @param TripalBundle $entity
 *   A tripal content type entity with information to be used for determining the default title format.
 * @param array $available_tokens
 *   An array of available tokens for this particular tripal content type.
 *
 * @return array
 *   An array of potential formats. The lightest weighted format suggested by all modules will be chosen.
 *   Each array item should consist of a 'weight' and 'format'. See the hook implementation below
 *   for examples.
 *    - weight: an integer used to determine priority of suggestions.
 *        The smaller/lighter the number the higher the priority.
 *        Best practice is to use a weight less than 0 for extension modules.
 *        specifically, -2 is a good weight for calculated formats and -5 is a
 *        good weight for hard-coded formats specific to a given type.
 *    - format: a string including approved tokens used to determine the title
 *        on Tripal content pages.
 */
function hook_tripal_default_title_format($entity, $available_tokens) {
  $format = array();

  // If you want to suggest a default format for a particular vocabulary term:
  //---------------------------------------------------------------------------
  // Load the term associated with this Tripal Content type.
  $term = entity_load('TripalTerm', array('id' => $entity->term_id));
  $term = reset($term);

  // If it's the term you are interested in then suggest a format.
  if ($term->name == 'organism') {

    // To suggest a format, add an element to the array with a format & weight key.
    $format[] = array(
      // This is the format/pattern you suggest be used to determine the title of organism pages.
      'format' => '[organism__genus] [organism__species]',
      // The weight/priority of your suggestion.
      'weight' => -5
    );

  }

  // Say you know that in your particular site, all 'names' are required
  // and you want to only use the human-readable name:
  //---------------------------------------------------------------------------
  $name_field = preg_grep('/__name]$/', array_keys($available_tokens));
  $name_field = reset($name_field);
  if (is_string($name_field)) {
    $format[] = array(
      'format' => $name_field,
      'weight' => -2,
    );
  }

  return $format;
}

/**
 * Returns an array of tokens based on Tripal Entity Fields.
 *
 * @param TripalBundle $entity
 *    The bundle entity for which you want tokens.
 * @return
 *    An array of tokens where the key is the machine_name of the token.
 */
function tripal_get_entity_tokens($entity, $options = array()) {
  $tokens = array();

  // Set default options.
  $options['required only'] = (isset($options['required only'])) ? $options['required only'] : FALSE;
  $options['include id'] = (isset($options['include id'])) ? $options['include id'] : TRUE;

  if ($options['include id']) {
    $token = '[TripalBundle__bundle_id]';
    $tokens[$token] = array(
      'label' => 'Bundle ID',
      'description' => 'The unique identifier for this Tripal Content Type.',
      'token' => $token,
      'field_name' => NULL,
      'required' => TRUE
    );

    $token = '[TripalEntity__entity_id]';
    $tokens[$token] = array(
      'label' => 'Content/Entity ID',
      'description' => 'The unique identifier for an individual piece of Tripal Content.',
      'token' => $token,
      'field_name' => NULL,
      'required' => TRUE
    );
  }

  $fields = field_info_instances('TripalEntity', $entity->name);
  foreach ($fields as $f) {

    // Build the token from the field information.
    $token = '[' . $f['field_name'] . ']';
    $current_token = array(
      'label' => $f['label'],
      'description' => $f['description'],
      'token' => $token,
      'field_name' => $f['field_name'],
      'required' => $f['required']
    );

    // If the required only option is set then we only want to add
    // required fields to the token list.
    if ($options['required only'] AND $current_token['required']) {
      $tokens[$token] = $current_token;
    }
    // If the required only option is not set then add everything.
    elseif (!$options['required only']) {
      $tokens[$token] = $current_token;
    }
  }

  return $tokens;
}

/**
 * Replace all Tripal Tokens in a given string.
 *
 * NOTE: If there is no value for a token then the token is removed.
 *
 * @param string $string
 *   The string containing tokens.
 * @param TripalEntity $entity
 *   The entity with field values used to find values of tokens.
 * @param TripalBundle $bundle_entity
 *   The bundle enitity containing special values sometimes needed for token replacement.
 *
 * @return
 *   The string will all tokens replaced with values.
 */
function tripal_replace_entity_tokens($string, &$entity, $bundle_entity = NULL) {
  // Determine which tokens were used in the format string
  $used_tokens = array();
  if (preg_match_all('/\[\w+\]/', $string, $matches)) {
    $used_tokens = $matches[0];
  }

  // If there are no tokens then just return the string.
  if (count($used_tokens) == 0) {
    return $string;
  }

  // If the field are not loaded for the entity then we want to load them
  // but we won't do a field_attach_load() as that will load all of the
  // fields. For syncing (publishing) of content loading all fields for
  // all synced entities causes extreme slowness, so we'll only attach
  // the necessary fields for replacing tokens.
  $attach_fields = array();
  foreach($used_tokens as $token) {
    $field_name = str_replace(array('.','[',']'), array('__','',''), $token);

    if (!property_exists($entity, $field_name)) {
      $field = field_info_field($field_name);
      $storage = $field['storage'];
      $attach_fields[$storage['type']]['storage'] = $storage;
      $attach_fields[$storage['type']]['fields'][]  = $field;
    }
  }

  // If we have any fields that need attaching, then do so now.
  if (count(array_keys($attach_fields)) > 0) {
    foreach ($attach_fields as $storage_type => $details) {
      $storage = $details['storage'];
      $fields = $details['fields'];
      $field_ids = array();
      foreach ($fields as $field) {
        $field_ids[$field['id']] = array($entity->id);
      }

      $entities = array($entity->id => $entity);
      module_invoke($storage['module'], 'field_storage_load', 'TripalEntity',
        $entities, FIELD_LOAD_CURRENT, $field_ids, array());
    }
  }

  // Now that all necessary fields are attached process the tokens.
  foreach($used_tokens as $token) {
    $value = '';
    if (isset($entity->{$field_name})) {

      // Render the value from the field.
      // First get the items to be rendered.
      $field_value = field_get_items('TripalEntity', $entity, $field_name);
      if (isset($field_value[0])) {
        // Then get a render array for just the value of the first item (no markup).
        $field_render_arr = field_view_value('TripalEntity', $entity, $field_name, $field_value[0]);
        // Finally render the value from the render array.
        $value = render($field_render_arr);
      }
    }
    // The TripalBundle__bundle_id is a special token for substituting the
    // bundle id.
    elseif ($field_name === 'TripalBundle__bundle_id') {
      // Load the bundle entity if we weren't given it.
      if (!$bundle_entity) {
        $bundle_entity = tripal_load_bundle_entity(array('name' => $entity->bundle));
      }
      // This token should be the id of the TripalBundle.
      $value = $bundle_entity->id;
    }
    // The TripalBundle__bundle_id is a special token for substituting the
    // entty id.
    elseif ($field_name === 'TripalEntity__entity_id') {
      // This token should be the id of the TripalEntity.
      $value = $entity->id;
    }

    // Perform the replacement of the token with the value.
    $string = str_replace($token, $value, $string);
  }

  return $string;
}

/**
 * Formats the tokens for display.
 *
 * @param array $tokens
 *   A list of tokens generated via tripal_get_entity_tokens().
 * @return
 *   Rendered output describing the available tokens.
 */
function theme_token_list($tokens) {

  $header = array('Token', 'Name', 'Description');
  $rows = array();
  foreach ($tokens as $details) {
    $rows[] = array(
      $details['token'],
      $details['label'],
      $details['description'],
    );
  }

  return theme('table', array('header' => $header, 'rows' => $rows));
}

/**
 * @section
 * Vocabulary Hooks.
 */

/**
 * A hook for specifying information about the data store for vocabularies.
 *
 * The storage backend for controlled vocabularies has traditionally been
 * the Chado CV term tables. However, Tripal v3.0 introduces APIs for supporting
 * other backends.  Therefore, this function indicates to Tripal which
 * data stores are capable of providing support for terms.
 *
 * @return
 *   An array describing the storage backends implemented by the module. The
 *   keys are storage backend names. To avoid name clashes, storage
 *   backend names should be prefixed with the name of the module that
 *   exposes them. The values are arrays describing the storage backend,
 *   with the following key/value pairs:
 *
 *   label: The human-readable name of the storage backend.
 *   module:  The name of the module providing the support for this backend.
 *   description: A short description for the storage backend.
 *   settings: An array whose keys are the names of the settings available for
 *     the storage backend, and whose values are the default values for
 *     those settings.
 */
function hook_vocab_storage_info() {
  return array(
    'term_chado_storage' => array(
      'label' => t('Chado storage'),
      'description' => t('Integrates terms stored in the local Chado database with Tripal entities.'),
      'settings' => array(),
    ),
  );
}


/**
 * Creates a form for specifying a term for TripalEntity creation.
 *
 * This hook allows the module that implements a vocabulary storage backend
 * to provide the form necessary to select a term that will then be used for
 * creating a new TripalEntity type.  Tripal will expect that a 'vocabulary' and
 * 'accession' are in the $form_state['storage'] array. The 'vocabulary' and
 * must be the abbreviated uppercase vocabulary for the vocabulary (e.g. 'RO',
 * 'SO', 'PATO', etc.).  The 'accession' must be the unique term ID (or
 * accession) for the term in the vocabulary.
 *
 * @param $form
 * @param $form_state
 *
 * @return
 *   A form object.
 */
function hook_vocab_select_term_form(&$form, &$form_state) {

  return $form;
}
/**
 * Validates the hook_vocab_select_term_form().
 *
 * @param $name
 */
function hook_vocab_select_term_form_validate($form, &$form_state) {

}
/**
 * Provides a form for importing vocabularies and their terms.
 *
 * Tripal allows for vocabularies to be stored separately from the biological
 * data. This hook allows the default term storage backend to provide an
 * approprite form for importing ontologies (either in OBO or OWL format).
 *
 * @param $form
 * @param $form_state
 *
 */
function hook_vocab_import_form($form, &$form_state) {
  return $form;
}
function hook_vocab_import_form_validate($form, &$form_state) {

}
function hook_vocab_import_form_submit($form, &$form_state) {

}

/**
 * Hook used by the default term storage backend to provide details for a term.
 *
 * This hook is called by the tripal_entity module to retrieve information
 * about the term from the storage backend.  It must return an array with
 * a set of keys.
 *
 * @param $vocabulary
 *   The vocabulary of the vocabulary in which the term is found.
 * @param $accession
 *   The unique identifier (accession) for this term.
 *
 * @return
 *   An array with at least the following keys:
 *     vocabulary : The short name of the vocabulary (e.g. SO, PATO, foaf).
 *     accession : The name unique ID of the term.
 *     url : The URL for the term.
 *     name : The name of the term.
 *     definition : The term's description.
 *   any other keys may be added as desired. Returns NULL if the term
 *   cannot be found.
 */
function hook_vocab_get_term($vocabulary, $accession) {
  // See the tripal_chado_vocab_get_term() function for an example.
}

/**
 * Hook used by the default term storage backend to add new terms.
 *
 * @param $values
 *   An associative array of key/value pairs used to add a new term.  The
 *   keys are:
 *     vocabulary : The vocabulary of the vocabulary.
 *     accession : The name unique ID of the term.
 *     url : The URL for the term.
 *     name : The name of the term.
 *     definition : The term's description.
 * @return
 *   TRUE if the term was added, FALSE otherwise.  If the term already exists
 *   it will be updated and the return value will be TRUE,
 */
function hook_vocab_set_term($values) {
  // See the tripal_chado_vocab_set_term() function for an example.
}


/**
 * Retrieves full information about a vocabulary term.
 *
 * Vocabularies are stored in a database backend.  Tripal has no requirements
 * for how terms are stored.  By default, the tripal_chado modules provides
 * storage for vocabularies and terms. This function will call the
 * hook_vocab_get_term() function for the database backend that is housing the
 * vocabularies and allow it to return the details about the term.
 *
 * @param $vocabulary
 *   The vocabulary of the vocabulary in which the term is found.
 * @param $accession
 *   The unique identifier (accession) for this term.
 *
 * @return
 *   An array with at least the following keys:
 *     vocabulary : The short name of the vocabulary (e.g. SO, PATO, foaf).
 *     accession : The name unique ID of the term.
 *     url : The URL for the term.
 *     name : The name of the term.
 *     definition : The term's description.
 *   any other keys may be added as desired. Returns NULL if the term
 *   cannot be found.
 */
function tripal_get_term_details($vocabulary, $accession) {
  // TODO: we need some sort of administrative interface that lets the user
  // switch to the desired vocabulary type. For now, we'll just use the
  // first one in the list.
  $stores = module_invoke_all('vocab_storage_info');
  if (is_array($stores) and count($stores) > 0) {
    $keys = array_keys($stores);
    $module = $stores[$keys[0]]['module'];
    $function = $module . '_vocab_get_term';
    if (function_exists($function)) {
      return $function($vocabulary, $accession);
    }
  }
}
